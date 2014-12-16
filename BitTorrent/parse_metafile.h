#ifndef PARSE_METAFILE
#define PARSE_METAFILE

// 保存从种子文件中获取的tracker的URL
typedef struct _Announce_list {
	char    announce[128];
	struct _Announce_list  *next;
} Announce_list;

// 保存各个待下载文件的路径和长度
typedef struct _Files {
	char    path[256];
	long    length;
	struct _Files *next;
} Files; 

int read_metafile(char *metafile_name);         // 读取种子文件
int find_keyword(char *keyword,long *position); // 在种子文件中查找某个关键词
int read_announce_list();                       // 获取各个tracker服务器的地址
int add_an_announce(char* url);                 // 向tracker列表添加一个URL

int get_piece_length();       // 获取每个piece的长度,一般为256KB
int get_pieces();             // 读取各个piece的哈希值

int is_multi_files();         // 判断下载的是单个文件还是多个文件
int get_file_name();          // 获取文件名，对于多文件，获取的是目录名
int get_file_length();        // 获取待下载文件的总长度
int get_files_length_path();  // 获取文件的路径和长度，对多文件种子有效

int get_info_hash();          // 由info关键词对应的值计算info_hash               
int get_peer_id();            // 生成peer_id,每个peer都有一个20字节的peer_id

// 释放parse_metafile.c中动态分配的内存
void release_memory_in_parse_metafile();
// 调用本文件中定义的函数,完成解析种子文件
int  parse_metafile(char *metafile);

#endif
