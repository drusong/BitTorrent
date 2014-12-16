#ifndef BTERROR_H
#define BTERROR_H

#define FILE_FD_ERR					-1  	// 无效的文件描述符
#define FILE_READ_ERR				-2  	// 读文件失败
#define FILE_WRITE_ERR				-3  	// 写文件失败
#define INVALID_METAFILE_ERR		-4  	// 无效的种子文件
#define INVALID_SOCKET_ERR			-5  	// 无效的套接字
#define INVALID_TRACKER_URL_ERR		-6  	// 无效的Tracker URL
#define INVALID_TRACKER_REPLY_ERR	-7  	// 无效的Tracker回应
#define INVALID_HASH_ERR			-8  	// 无效的hash值
#define INVALID_MESSAGE_ERR			-9  	// 无效的消息
#define INVALID_PARAMETER_ERR		-10 	// 无效的函数参数
#define FAILED_ALLOCATE_MEM_ERR		-11 	// 申请动态内存失败
#define NO_BUFFER_ERR				-12		// 没有足够的缓冲区
#define READ_SOCKET_ERR				-13 	// 读套接字失败
#define WRITE_SOCKET_ERR			-14 	// 写套接字失败
#define RECEIVE_EXIT_SIGNAL_ERR		-15 	// 接收到退出程序的信号


// 用于提示致命性的错误,程序将终止
void btexit(int errno,char *file,int line);

#endif
