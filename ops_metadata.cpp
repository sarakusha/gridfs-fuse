/*
 *  Copyright 2009 Michael Stephens
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <pwd.h>
#include <grp.h>

#include "operations.h"
#include "options.h"
#include "utils.h"

int gridfs_getattr(const char *path, struct stat *stbuf) {
  memset(stbuf, 0, sizeof(struct stat));

  if (strcmp(path, "/") == 0) {
    stbuf->st_mode = S_IFDIR | 0777;
    stbuf->st_nlink = 2;
    return 0;
  }

  path = fuse_to_mongo_path(path);
  auto file_iter = open_files.find(path);

  if (file_iter != open_files.end()) {
    LocalGridFile::ptr lgf = file_iter->second;
    stbuf->st_mode = S_IFREG | (lgf->Mode() & (0xffff ^ S_IFMT));
    stbuf->st_nlink = 1;
    stbuf->st_uid = lgf->Uid();
    stbuf->st_gid = lgf->Gid();
    stbuf->st_ctime = time(NULL);
    stbuf->st_mtime = time(NULL);
    stbuf->st_size = lgf->Length();
    return 0;
  }

  // HACK: This is a protective measure to ensure we don't spin off into forever if a path without a period is requested.
  if (path_depth(path) > 10)
    return -ENOENT;

  // HACK: Assumes that if the last part of the path has a '.' in it, it's the leaf of the path, and if we haven't found a match by now,
  // give up and go home. This works just dandy as long as you avoid putting periods in your 'directory' names.
  /*if (!is_leaf(path)) {
    stbuf->st_mode = S_IFDIR | 0777;
    stbuf->st_nlink = 2;
    return 0;
  }*/

  auto sdc = make_ScopedDbConnection();
  mongo::BSONObj file_obj = sdc->conn().findOne(db_name() + ".files",
						BSON("filename" << path));

  if (file_obj.isEmpty())
    return -ENOENT;

  stbuf->st_mode = S_IFREG | (file_obj["mode"].Int() & (0xffff ^ S_IFMT));
  stbuf->st_nlink = 1;
  if (!file_obj["owner"].eoo()) {
    passwd *pw = getpwnam(file_obj["owner"].str().c_str());
    if (pw)
      stbuf->st_uid = pw->pw_uid;
  }
  if (!file_obj["group"].eoo()) {
    group *gr = getgrnam(file_obj["group"].str().c_str());
    if (gr)
      stbuf->st_gid = gr->gr_gid;
  }
  stbuf->st_size = file_obj["length"].Int();

  time_t upload_time = mongo_time_to_unix_time(file_obj["uploadDate"].date());
  stbuf->st_ctime = upload_time;
  stbuf->st_mtime = upload_time;

  return 0;
}

int gridfs_chmod(const char* path, mode_t mode) {
  path = fuse_to_mongo_path(path);
  auto file_iter = open_files.find(path);

  if (file_iter != open_files.end()) {
    LocalGridFile::ptr lgf = file_iter->second;
    lgf->setMode(mode);
  }

  auto sdc = make_ScopedDbConnection();
  sdc->conn().update(db_name() + ".files",
		     BSON("filename" << path),
		     BSON("$set" << BSON("mode" << mode)));

  return 0;
}

int gridfs_chown(const char* path, uid_t uid, gid_t gid) {
  path = fuse_to_mongo_path(path);
  auto file_iter = open_files.find(path);

  if (file_iter != open_files.end()) {
    LocalGridFile::ptr lgf = file_iter->second;
    lgf->setUid(uid);
    lgf->setGid(gid);
  }

  mongo::BSONObjBuilder b;

  {
    passwd *pw = getpwuid(uid);
    if (pw)
      b.append("owner", pw->pw_name);
  }
  {
    group *gr = getgrgid(gid);
    if (gr)
      b.append("group", gr->gr_name);
  }

  if (b.hasField("owner") || b.hasField("group")) {
    auto sdc = make_ScopedDbConnection();
    sdc->conn().update(db_name() + ".files",
		       BSON("filename" << path),
		       BSON("$set" << b.obj()));
  }

  return 0;
}

int gridfs_utimens(const char* path, const struct timespec tv[2]) {
  path = fuse_to_mongo_path(path);

  unsigned long long millis = ((unsigned long long)tv[1].tv_sec * 1000) + (tv[1].tv_nsec / 1e+6);

  auto sdc = make_ScopedDbConnection();
  sdc->conn().update(db_name() + ".files",
		     BSON("filename" << path),
		     BSON("$set" <<
			  BSON("uploadDate" << mongo::Date_t(millis))
			  ));

  return 0;
}

int gridfs_rename(const char* old_path, const char* new_path) {
  old_path = fuse_to_mongo_path(old_path);
  new_path = fuse_to_mongo_path(new_path);

  auto sdc = make_ScopedDbConnection();
  mongo::DBClientBase &client = sdc->conn();

  mongo::BSONObj file_obj = client.findOne(db_name() + ".files",
				    BSON("filename" << old_path));

  if (file_obj.isEmpty())
    return -ENOENT;

  mongo::BSONObjBuilder b;
  std::set<std::string> field_names;
  file_obj.getFieldNames(field_names);

  for (auto name : field_names) {
    if (name != "filename") {
      b.append(file_obj.getField(name));
    }
  }

  b << "filename" << new_path;

  client.update(db_name() + ".files",
		BSON("_id" << file_obj.getField("_id")),
		b.obj());

  return 0;
}
