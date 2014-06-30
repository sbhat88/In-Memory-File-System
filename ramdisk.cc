#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <stdlib.h>
#include <map>
#include <list>

#define GET_PARENT_DIR 1
#define GET_DIR 2
#define true 1

using namespace std;

class fileInfo 
{
  public:
    string fileName;
    string fileContent;
    mode_t mode;
    time_t updateTime;
     
    fileInfo(string filename)
    {
        fileName = filename;
        fileContent = "";
	updateTime = time(NULL);
    }
    
    fileInfo(string filename, string content)
    {
        fileName = filename;
        fileContent = content;
	updateTime = time(NULL);
    }
    
    int length()
    {
        return fileContent.length();
    }
};

time_t rootCreateTime;

class dirInfo_t
{
  public:
	string dirName;
	map<string, fileInfo*> fileList;
	dirInfo_t* parentDir;
	map<string, dirInfo_t*> childDir;       
	map<string, time_t> fileTime;
	time_t createTime;

	dirInfo_t()
	{
		parentDir = NULL;
		dirName = "/";
	}

	int updateFileTime(string filename);
};

typedef map<string, fileInfo*>::iterator fileListIter;
typedef map<string, dirInfo_t*>::iterator dirListIter;

int dirInfo_t::updateFileTime(string filename)
{
	fileList[filename]->updateTime = time(NULL);
}


dirInfo_t* currentDirectory = NULL;  
long curFsSize = 0;
long maxfsSize;


//////////////////////////////////////////////////////////

list<string> parsePath(string path)
{
	list<string> parsedPath;
	char* p;
	char* cPath = strdup(path.c_str());
	
	p = strtok(cPath, "/");
	
	while (p != NULL)
	{
		parsedPath.push_back(p);
		p = strtok(NULL, "/");
	}
	
	free(cPath);
	return parsedPath;
}

string getFileOrDirName(string path)
{
	list<string> pathList = parsePath(path);
	return pathList.back();
}

dirInfo_t* getDirectory(string path, int type)
{
	dirInfo_t* dInfo = currentDirectory;

	list<string> mylist = parsePath(path);
	if ((mylist.size() == 1) && (type == GET_PARENT_DIR))
	{
	    return dInfo;
	}
        
	list<string>::iterator it = mylist.begin();
	int size = mylist.size();
	if (type == GET_PARENT_DIR)
	    size--;

	for (int i = 0;i < size; it++,i++)
	{
		dirListIter iter = dInfo->childDir.find(*it);
		if (iter == dInfo->childDir.end())
		    return NULL;

		dInfo = iter->second;     
	}

	return dInfo;
	
}

int findFile(const char* path)
{
	dirInfo_t* dInfo = getDirectory(path, GET_PARENT_DIR);
	if (dInfo == NULL)
		return 0;

	string sPath = getFileOrDirName(path);
    	fileListIter it = dInfo->fileList.begin();
	for (;it != dInfo->fileList.end(); it++)
	{
		if (!strcmp(sPath.c_str(), it->first.c_str()))
			return 1;
		
	}
	return 0;
}

static int ramd_getattr(const char *path, struct stat *stbuf)
{
	int retValue = 0;

	memset(stbuf, 0, sizeof(struct stat));

	dirInfo_t* dInfo = NULL;
	dInfo = getDirectory(path, GET_DIR);//check whether its a dir

	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 3;  //should be 3 bec of hierarchical dir (. and ..)
		stbuf->st_mtime	= rootCreateTime;
		stbuf->st_size = 4096;
	}
	else if (dInfo != NULL)
	{
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 3;  //should be 3 bec of hierarchical dir (. and ..)
		stbuf->st_mtime	= dInfo->createTime;
		stbuf->st_size = 4096;
	}
	else if (findFile(path) == true)
	{
		dInfo = getDirectory(path, GET_PARENT_DIR);
		if (dInfo == NULL)
			return -ENOENT;

		stbuf->st_mode = S_IFREG | 0755;
		stbuf->st_nlink = 1;
		string filename = getFileOrDirName(path);
		stbuf->st_size = dInfo->fileList[filename]->length();
        
		stbuf->st_mtime = dInfo->fileList[filename]->updateTime;
	}
	else
		retValue = -ENOENT;

	return retValue;
}

static int ramd_opendir(const char * path, struct fuse_file_info *fileInfo)
{
	return 0;
}

static int ramd_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fileInfo)
{
	(void) offset;
	(void) fileInfo;
	
	dirInfo_t* dInfo = getDirectory(path, GET_DIR);
	if (dInfo == NULL)
		dInfo = currentDirectory; //if no child dir then read curr dir

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	dirListIter it;
	for (it = dInfo->childDir.begin(); it != dInfo->childDir.end(); it++)
	{
		filler(buf, (it->first).c_str(), NULL, 0);
	}

    	fileListIter iter;
	for (iter = dInfo->fileList.begin(); iter != dInfo->fileList.end(); iter++)
	{
		filler(buf, (iter->first).c_str(), NULL, 0);
	}

	return 0;
}

static int ramd_open(const char *path, struct fuse_file_info *fileInfo)
{
	if (findFile(path) == 0)
		return -ENOENT;

	return 0;
}

static int ramd_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fileInfo)
{
	(void) fileInfo;
	size_t length;

	if (findFile(path) == 0)
		return -ENOENT;

	dirInfo_t* dInfo = getDirectory(path, GET_PARENT_DIR);
	if (dInfo == NULL)
		dInfo = currentDirectory;

	string filename = getFileOrDirName(path);
	
    	fileListIter it = dInfo->fileList.find(filename);
	if (it == dInfo->fileList.end())
		return -ENOENT;

	length = (it->second)->length();
	
	if (offset < length)
	{
		if (offset + size > length)
			size = length - offset;
		
		memcpy(buf, (it->second)->fileContent.c_str() + offset, size);
	}
	else
		size = 0;

	return size;
}

static int ramd_write(const char *path, const char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fileInfo)
{
	(void) fileInfo;
	
	if (findFile(path) == 0)
		return -ENOENT;

	dirInfo_t* dInfo = getDirectory(path, GET_PARENT_DIR);
	if (dInfo == NULL)
		dInfo = currentDirectory;

	if (curFsSize+size > maxfsSize)
		return -ENOSPC;

	string filename = getFileOrDirName(path);
	
    	fileListIter it = dInfo->fileList.find(filename);
	if (it == dInfo->fileList.end())
		return -ENOENT;
    
	it->second->fileContent.append(const_cast<char*>(buf), size);
	curFsSize += size;

	return size;
}

static int ramd_create(const char *path, mode_t mode, struct fuse_file_info *fileInf)
{
	(void) fileInf;

	dirInfo_t* dInfo = getDirectory(path, GET_PARENT_DIR);
	if (dInfo == NULL)
		dInfo = currentDirectory;

	string filename = getFileOrDirName(path);
    	fileInfo* fInfo = new fileInfo(filename);

    	dInfo->fileList.insert(std::pair<string, fileInfo*>(filename, fInfo));
	dInfo->updateFileTime(filename);
	
	return 0;
}

static int ramd_unlink(const char *path)
{
	if (findFile(path) == 0)
		return -ENOENT;

	dirInfo_t* dInfo = getDirectory(path, GET_PARENT_DIR);
	if (dInfo == NULL)
		dInfo = currentDirectory;

	string filename = getFileOrDirName(path);
	curFsSize -= dInfo->fileList[filename]->length();
	int ret = dInfo->fileList.erase(filename);
	
	return 0;
}

static int ramd_mkdir(const char * path, mode_t mode)
{
	dirInfo_t* dInfo = new dirInfo_t;

	dirInfo_t* drInfo = getDirectory(path, GET_PARENT_DIR);
	if (drInfo == NULL)
		drInfo = currentDirectory;

	string dirName = getFileOrDirName(path);
	dInfo->dirName = dirName;
	dInfo->createTime = time(NULL);
	drInfo->childDir[dirName] = dInfo;
	curFsSize += 4096;

	return 0;
}

static int ramd_rmdir(const char * path)
{
	dirInfo_t* drInfo = getDirectory(path, GET_PARENT_DIR);
	if (drInfo == NULL)
		return 0;

	dirInfo_t* curDir = getDirectory(path, GET_DIR);
	if (curDir != NULL)
	{
		if (curDir->fileList.empty() == 0)    //if rmdir called on a non empty dir
			return -ENOTEMPTY;

		if (curDir->childDir.empty() == 0)
			return -ENOTEMPTY;
	}
	
	string dirName = getFileOrDirName(path);
	drInfo->childDir.erase(dirName);
	curFsSize -= 4096;
	return 0;
}

static int ramd_truncate(const char* path, off_t length)
{
	if (findFile(path) == 0)
		return -ENOENT;

	dirInfo_t* dInfo = getDirectory(path, GET_PARENT_DIR);
	if (dInfo == NULL)
		return 0;

	string filename = getFileOrDirName(path);
	
    	fileListIter it = dInfo->fileList.find(filename);
	if (it != dInfo->fileList.end())
	{
		string sData = dInfo->fileList[filename]->fileName;
		dInfo->fileList[filename]->fileName = sData.substr(1, length);
	}
	return 0;
}

static int ramd_utimens(const char * path, const struct timespec tv[2])
{
	return 0;
}

static int ramd_rename(const char * path, const char *newname)
{
	
	dirInfo_t* dInfo = NULL;

	dInfo = getDirectory(path, GET_DIR);//check if the path is a dir
	if (dInfo == NULL)
	{
		if (findFile(path) == 0) //check whether file exist
			return -ENOENT;
		dInfo = getDirectory(path, GET_PARENT_DIR);//get the parent dir
		if (dInfo == NULL)
			return -ENOENT;

		string filename = getFileOrDirName(path);
		string data = dInfo->fileList[filename]->fileContent;
		dInfo->fileList.erase(filename);

		//move to another dir
		dInfo = getDirectory(newname, GET_PARENT_DIR);
		if (dInfo == NULL)
			return -ENOENT;
        
		filename = getFileOrDirName(newname);
	        fileInfo* fInfo = new fileInfo(newname, data);
		dInfo->fileList[filename] = fInfo;
	}
	else
	{
		
		dInfo = getDirectory(path, GET_PARENT_DIR); //get parent dir
		if (dInfo == NULL)
			return -ENOENT;

		string dirName = getFileOrDirName(path);
		dirInfo_t* tmp = dInfo->childDir[dirName];
		dInfo->childDir.erase(dirName);
		
		dInfo = getDirectory(newname, GET_PARENT_DIR);
		if (dInfo == NULL)
			return -ENOENT;

		dirName = getFileOrDirName(newname);
		tmp->dirName = dirName;
		dInfo->childDir[dirName] = tmp;
		
	}
	
	return 0;
}

 int ramd_statfs(const char *path, struct statvfs *statbuf)
{
	statbuf->f_bsize = 1;
	statbuf->f_frsize = 1;
	statbuf->f_blocks = maxfsSize;
	statbuf->f_bfree = maxfsSize - curFsSize;
	statbuf->f_bavail = maxfsSize - curFsSize;
	return 0;
}

/////////////////////////////////////////////////////////
static struct fuse_operations ramd_oper;

int main(int argc, char *argv[])
{
	ramd_oper.getattr           = ramd_getattr;
	ramd_oper.readdir           = ramd_readdir;
	ramd_oper.open              = ramd_open;
	ramd_oper.read              = ramd_read;
	ramd_oper.write             = ramd_write;
	ramd_oper.create            = ramd_create;
	ramd_oper.unlink            = ramd_unlink;
	ramd_oper.mkdir             = ramd_mkdir;
	ramd_oper.rmdir             = ramd_rmdir;
	ramd_oper.truncate          = ramd_truncate;
	ramd_oper.utimens           = ramd_utimens;
	ramd_oper.rename            = ramd_rename;
	ramd_oper.statfs            = ramd_statfs;
	ramd_oper.opendir           = ramd_opendir;
	
	currentDirectory = new dirInfo_t;
	
	rootCreateTime = time(NULL);
	
	if (argc == 4)
	{
		maxfsSize = atoi(argv[argc-2]);
		maxfsSize = maxfsSize*1024*1024;
		argv[2] = argv[3];
		return fuse_main(argc-1, argv, &ramd_oper, NULL);
	}
	else if (argc == 3)
	{
		maxfsSize = atoi(argv[argc-1]);
		maxfsSize = maxfsSize*1024*1024;
		return fuse_main(argc-1, argv, &ramd_oper, NULL);
	} 
	else
	{
		printf("Enter <mount path>, <filesystem size>\n");
		exit(0);
	}

}
