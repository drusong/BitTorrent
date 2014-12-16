#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>
#include "data.h"
#include "sha1.h"
#include "parse_metafile.h"
#include "bitfield.h"
#include "message.h"
#include "policy.h"
#include "torrent.h"

// 对定义的缓冲区的说明：
// 设置缓冲区可以避免频繁读写硬盘,从而有利于保护硬盘
// 每个缓冲区结点的大小为16KB,默认生成1024个结点,总大小为16MB
// 缓冲区以256KB为单位使用,也就是临近的16个结点为一组存放一个piece
// 下标为0～15的结点存放一个piece,16～31存放一个piece,依次类推
// 也可以处理一个piece的大小不为256KB的情况,如一个piece大小为512KB
// 为了处理的方便,所有缓冲区在程序启动时统一申请,在程序结束时释放

// 缓冲区中共有多少个Btcache结点
#define btcache_len 1024

// 以下变量定义在parse_metafile.c文件
extern  char   *file_name;
extern  Files  *files_head;
extern  int     file_length;
extern  int     piece_length;
extern  int     pieces_length;
extern  char   *pieces;

extern  Bitmap *bitmap;
extern  int     download_piece_num;
extern  Peer   *peer_head;

// 指向一个16MB大小的缓冲区
Btcache *btcache_head = NULL;
// 存放待下载文件的最后一个piece
Btcache *last_piece = NULL;
int      last_piece_index = 0;
int      last_piece_count = 0;
int      last_slice_len   = 0;

// 存放文件描述符
int *fds    = NULL;
int fds_len = 0;

// 存放刚刚下载到的piece的索引
// 下载到一个新的piece要向所有的peer通报
int have_piece_index[64]; 

// 是否进入了终端模式
int end_mode = 0;

// 为Btcache节点分配内存空间
Btcache* initialize_btcache_node()
{
	Btcache *node;

	node = (Btcache *)malloc(sizeof(Btcache));
	if(node == NULL) {
		printf("%s:%d malloc error\n",__FILE__,__LINE__);
		return NULL;
	}

	node->buff = (unsigned char *)malloc(16*1024);
	if(node->buff == NULL) {
		if(node != NULL)  free(node);
		printf("%s:%d malloc error\n",__FILE__,__LINE__);
		return NULL;
	}

	node->index  = -1;
	node->begin  = -1;
	node->length = -1;

	node->in_use       =  0;
	node->read_write   = -1;
	node->is_full      =  0;
	node->is_writed    =  0;
	node->access_count =  0;
	node->next         =  NULL;

	return node;
}

// 创建总大小为16K*1024即16MB的缓冲区
int create_btcache()
{
	int     i;
	Btcache *node, *last;

	for(i = 0; i < btcache_len; i++) 
	{
		node = initialize_btcache_node();
		if( node == NULL )  
		{ 
			printf("%s:%d create_btcache error\n",__FILE__,__LINE__);
			release_memory_in_btcache();
			return -1;
		}

		if( btcache_head == NULL )  
		{ 
			btcache_head = node; 
			last = node; 
		}
		else  
		{ 
			last->next = node; 
			last = node; 
		}
	}

	int count = file_length % piece_length / (16*1024);
	if(file_length % piece_length % (16*1024) != 0)  count++;
	last_piece_count = count;

	last_slice_len = file_length % piece_length % (16*1024);
	if(last_slice_len == 0)  last_slice_len = 16*1024;
	
	last_piece_index = pieces_length / 20 -1;

	while(count > 0) 
	{
		node = initialize_btcache_node();
		if(node == NULL) 
		{
			printf("%s:%d create_btcache error\n",__FILE__,__LINE__);
			release_memory_in_btcache();
			return -1;
		}

		if(last_piece == NULL)  
		{ 
			last_piece = node; 
			last = node; 
		}
		else  
		{ 
			last->next = node; 
			last = node; 
		}
		count--;
	}

	for(i = 0; i < 64; i++) 
	{
		have_piece_index[i] = -1;
	}

	return 0;
}

// 释放缓冲区动态分配的内存
void release_memory_in_btcache()
{
	Btcache *p;

	p = btcache_head;
	while(p != NULL) 
	{
		btcache_head = p->next;
		if(p->buff != NULL) 
			free(p->buff);
		free(p);
		p = btcache_head;
	}

	release_last_piece();
	if(fds != NULL)  
		free(fds);
}

void release_last_piece()
{
	Btcache *p = last_piece;

	while(p != NULL) 
	{
		last_piece = p->next;
		if(p->buff != NULL) 
			free(p->buff);
		free(p);
		p = last_piece;
	}
}

// 判断种子文件中待下载的文件个数
int get_files_count()
{
	int count = 0;
	
	if(is_multi_files() == 0)  return 1;

	Files *p = files_head;
	while(p != NULL) 
	{
		count++;
		p = p->next;
	}
	
	return count;
}

// 根据种子文件中的信息创建保存下载数据的文件
// 通过lseek和write两个函数来实现物理存储空间的分配
int create_files()
{
	int  ret, i;
	char buff[1] = { 0x0 };
	
	fds_len = get_files_count();
	if(fds_len < 0)  return -1;
	
	fds = (int *)malloc(fds_len * sizeof(int));
	if(fds == NULL)  return -1;
	
	
	if( is_multi_files() == 0 )    // 待下载的为单文件
    {
		*fds = open(file_name,O_RDWR|O_CREAT,0777);
		if(*fds < 0)  
		{ 
			printf("%s:%d error",__FILE__,__LINE__); 
			return -1; 
		}
		ret = lseek(*fds,file_length-1,SEEK_SET);
		if(ret < 0)   
		{ 
			printf("%s:%d error",__FILE__,__LINE__); 
			return -1; 
		}
		
		ret = write(*fds,buff,1);
		if(ret != 1)  
		{ 
			printf("%s:%d error",__FILE__,__LINE__); 
			return -1; 
		}
	} 
	else 
	{  // 待下载的是多个文件				
		// 查看目录是否已创建,若没有则创建
		ret = chdir(file_name);
		if(ret < 0) 
		{
			ret = mkdir(file_name,0777);
			if(ret < 0)  { printf("%s:%d error",__FILE__,__LINE__); return -1; }
			ret = chdir(file_name);
			if(ret < 0)  { printf("%s:%d error",__FILE__,__LINE__); return -1; }
		}
					
		Files *p = files_head;
		i = 0;
		while(p != NULL) 
		{
			fds[i] = open(p->path,O_RDWR|O_CREAT,0777);
			if(fds[i] < 0) 
			{
				printf("%s:%d error",__FILE__,__LINE__); 
				return -1;
			}
					
			ret = lseek(fds[i],p->length-1,SEEK_SET);
			if(ret < 0)    
			{
				printf("%s:%d error",__FILE__,__LINE__); 
				return -1;
			}
					
			ret = write(fds[i],buff,1);
			if(ret != 1)   
			{
				printf("%s:%d error",__FILE__,__LINE__); 
				return -1;
			}
				
			p = p->next;
			i++;
		} //end while
	} //end else

	return 0;
}

// 判断一个Btcache结点(即一个slice)中的数据要写到哪个文件的哪个位置,并写入
int write_btcache_node_to_harddisk(Btcache *node)
{
	long long     line_position;
	Files         *p;
	int           i;

	if((node == NULL) || (fds == NULL))  return -1;

	// 无论是否下载多文件，将要下载的所有数据看成一个线性字节流
	// line_position指示要写入硬盘的线性位置
	// piece_length为每个piece长度，它被定义在parse_metafile.c中
	line_position = node->index * piece_length + node->begin;

	if( is_multi_files() == 0 ) 
	{  // 如果下载的是单个文件
		lseek(*fds,line_position,SEEK_SET);
		write(*fds,node->buff,node->length);
		return 0;
	}

	// 下载的是多个文件
	if(files_head == NULL) 
	{ 
		printf("%s:%d file_head is NULL",__FILE__,__LINE__);
		return -1;
	}
	p = files_head;
	i = 0;
	while(p != NULL) 
	{
		if((line_position < p->length) && (line_position+node->length < p->length)) 
		{
			// 待写入的数据属于同一个文件
			lseek(fds[i],line_position,SEEK_SET);
			write(fds[i],node->buff,node->length);
			break;
		} 
		else if((line_position < p->length) && (line_position+node->length >= p->length)) 
		{
			// 待写入的数据跨越了两个文件或两个以上的文件
			int offset = 0;             // buff内的偏移,也是已写的字节数
			int left   = node->length;  // 剩余要写的字节数
			
			lseek(fds[i],line_position,SEEK_SET);
			write(fds[i],node->buff,p->length - line_position);
			offset = p->length - line_position;        // offset存放已写的字节数
			left = left - (p->length - line_position); // 还需写在字节数
			p = p->next;                               // 用于获取下一个文件的长度
			i++;                                       // 获取下一个文件描述符
			
			while(left > 0)
			{
				if(p->length >= left) 
				{  // 当前文件的长度大于等于要写的字节数 
					lseek(fds[i],0,SEEK_SET);
					write(fds[i],node->buff+offset,left); // 写入剩余要写的字节数
					left = 0;
				} 
				else 
			    {  // 当前文件的长度小于要写的字节数
					lseek(fds[i],0,SEEK_SET);
					write(fds[i],node->buff+offset,p->length); // 写满当前文件
					offset = offset + p->length;
					left = left - p->length;
					i++;
					p = p->next;
				}
			}	
			break;
		} 
		else 
		{
			// 待写入的数据不应写入当前文件
			line_position = line_position - p->length;
			i++;
			p = p->next;
		}
	}
	return 0;
}

// 从硬盘读出数据，存放到缓冲区中，在peer需要时发送给peer
// 该函数非常类似于write_btcache_node_to_harddisk
// 要读的piece的索引index，在piece中的起始位置begin和长度已存到node指向的节点中
int read_slice_from_harddisk(Btcache *node)
{
	unsigned int  line_position;
	Files         *p;
	int           i;
	
	if( (node == NULL) || (fds == NULL) )  return -1;
	
	if( (node->index >= pieces_length/20) || (node->begin >= piece_length) ||
	           (node->length > 16*1024) )
		return -1;

	// 计算线性偏移量
	line_position = node->index * piece_length + node->begin;
	
	if( is_multi_files() == 0 ) 
	{  // 如果下载的是单个文件
		lseek(*fds,line_position,SEEK_SET);
		read(*fds,node->buff,node->length);
		return 0;
	}
	
	// 如果下载的是多个文件
	if(files_head == NULL)  get_files_length_path();
	p = files_head;
	i = 0;
	while(p != NULL) 
	{
		if((line_position < p->length) && (line_position+node->length < p->length)) 
		{
			// 待读出的数据属于同一个文件
			lseek(fds[i],line_position,SEEK_SET);
			read(fds[i],node->buff,node->length);
			break;
		} 
		else if((line_position < p->length) && (line_position+node->length >= p->length)) 
		{
			// 待读出的数据跨越了两个文件或两个以上的文件
			int offset = 0;             // buff内的偏移,也是已读的字节数
			int left   = node->length;  // 剩余要读的字节数

			lseek(fds[i],line_position,SEEK_SET);
			read(fds[i],node->buff,p->length - line_position);
			offset = p->length - line_position;        // offset存放已读的字节数
			left = left - (p->length - line_position); // 还需读在字节数
			p = p->next;                               // 用于获取下一个文件的长度
			i++;                                       // 获取下一个文件描述符

			while(left > 0)
			{
				if(p->length >= left) 
				{  // 当前文件的长度大于等于要读的字节数 
					lseek(fds[i],0,SEEK_SET);
					read(fds[i],node->buff+offset,left); // 读取剩余要读的字节数
					left = 0;
				} 
				else 
				{  // 当前文件的长度小于要读的字节数
					lseek(fds[i],0,SEEK_SET);
					read(fds[i],node->buff+offset,p->length); // 读取当前文件的所有内容
					offset = offset + p->length;
					left = left - p->length;
					i++;
					p = p->next;
				}
		   } 
		   break;
		}
		else 
		{
			// 待读出的数据不应写入当前文件
			line_position = line_position - p->length;
			i++;
			p = p->next;
		}
	}
	return 0;
}

// 在peer队列中删除对某个piece的请求
int delete_request_end_mode(int index)
{
	Peer          *p = peer_head;
	Request_piece *req_p, *req_q;

	if(index < 0 || index >= pieces_length/20)  
		return -1;

	while(p != NULL) 
	{
		req_p = p->Request_piece_head;
		while(req_p != NULL) 
		{
			if(req_p->index == index) 
			{
				if(req_p == p->Request_piece_head) 
					p->Request_piece_head = req_p->next;
				else 
					req_q->next = req_p->next;
				free(req_p);

				req_p = p->Request_piece_head; /***why not  req_p = req_q->next ? **/
				continue;
			}
			req_q = req_p;
			req_p = req_p->next;
		}
		
		p = p->next;
	}

	return 0;
}

// 检查一个piece的数据是否正确,若正确存入硬盘
int write_piece_to_harddisk(int sequnce,Peer *peer)
{
	Btcache        *node_ptr = btcache_head, *p;
	unsigned char  piece_hash1[20], piece_hash2[20];
	int            slice_count = piece_length / (16*1024);
	int            index, index_copy;

	if(peer==NULL) return -1;

	int i = 0;
	while(i < sequnce)  
	{ 
		node_ptr = node_ptr->next; 
		i++; 
	}
	p = node_ptr;  // p指向piece的第一个slice所在的btcache结点

	// 校验piece的HASH值
	SHA1_CTX ctx;
	SHA1Init(&ctx);
	while(slice_count>0 && node_ptr!=NULL) 
	{
		SHA1Update(&ctx,node_ptr->buff,16*1024);
		slice_count--;
		node_ptr = node_ptr->next;
	}
	SHA1Final(piece_hash1,&ctx);
	
	index = p->index * 20;
	index_copy = p->index;  // 存放piece的index
	for(i = 0; i < 20; i++)  piece_hash2[i] = pieces[index+i];

	int ret = memcmp(piece_hash1,piece_hash2,20);
	if(ret != 0)  
	{ 
		printf("piece hash is wrong\n"); 
		return -1; 
	}
	
	node_ptr = p;
	slice_count = piece_length / (16*1024); 
	while(slice_count > 0) 
	{
		write_btcache_node_to_harddisk(node_ptr);

		// 在peer中的请求队列中删除piece请求
		Request_piece *req_p = peer->Request_piece_head;
		Request_piece *req_q = peer->Request_piece_head;
		while(req_p != NULL) 
		{
			if(req_p->begin==node_ptr->begin && req_p->index==node_ptr->index)
			{
				if(req_p == peer->Request_piece_head) 
					peer->Request_piece_head = req_p->next;
				else
					req_q->next = req_p->next;
				free(req_p);
				req_p = req_q = NULL;
				break;
			}
			req_q = req_p;
			req_p = req_p->next;
		}

		node_ptr->index  = -1;
		node_ptr->begin  = -1;
		node_ptr->length = -1;
		
		node_ptr->in_use       = 0;
		node_ptr->read_write   = -1;
		node_ptr->is_full      = 0;
		node_ptr->is_writed    = 0;
		node_ptr->access_count = 0;

		node_ptr = node_ptr->next;
		slice_count--;
	}
	
	if(end_mode == 1)  delete_request_end_mode(index_copy);

	// 更新位图
	set_bit_value(bitmap,index_copy,1);

	// 准备发送have消息
	for(i = 0; i < 64; i++) 
	{
		if(have_piece_index[i] == -1) 
		{ 
			have_piece_index[i] = index_copy; 
			break; 
		}
	}

	download_piece_num++;
	if(download_piece_num % 10 == 0)  restore_bitmap();

	printf("%%%%%% Total piece download:%d %%%%%%\n",download_piece_num);
	printf("writed piece index:%d  total pieces:%d\n",index_copy,pieces_length/20);
	compute_total_rate();   // 计算总的下载、上传速度
	print_process_info();   // 打印下载进度信息

	return 0;
}

// 从硬盘上读取一个piece到p所指向的缓冲区中
int read_piece_from_harddisk(Btcache *p, int index)
{
	Btcache  *node_ptr   = p;
	int      begin       = 0;
	int      length      = 16*1024;
	int      slice_count = piece_length / (16*1024);
	int      ret;

	if(p==NULL || index>=pieces_length/20)  return -1;

	while(slice_count > 0) 
	{
		node_ptr->index  = index;
		node_ptr->begin  = begin;
		node_ptr->length = length;

		ret = read_slice_from_harddisk(node_ptr);
		if(ret < 0) return -1;

		node_ptr->in_use       = 1;
		node_ptr->read_write   = 0;
		node_ptr->is_full      = 1;
		node_ptr->is_writed    = 0;
		node_ptr->access_count = 0;

		begin += 16*1024;
		slice_count--;
		node_ptr = node_ptr->next;
	}

	return 0;
}

// 将16MB缓冲区中已下载的piece写入硬盘,这样可以释放缓冲区
int write_btcache_to_harddisk(Peer *peer)
{
	Btcache          *p = btcache_head;
	int     slice_count = piece_length / (16*1024);
	int     index_count = 0;
	int      full_count = 0;
	int     first_index;

	while(p != NULL) 
	{
		if(index_count % slice_count == 0) 
		{
			full_count = 0;
			first_index = index_count;
		}

		if( (p->in_use  == 1) && (p->read_write == 1) && 
			    (p->is_full == 1) && (p->is_writed  == 0) ) 
		{
			full_count++;
		}

		if(full_count == slice_count) 
		{
			write_piece_to_harddisk(first_index,peer);
		}

		index_count++;
		p = p->next;
	}

	return 0;
}

// 当缓冲区不够用时,释放那些从硬盘上读取的piece
int release_read_btcache_node(int base_count)
{
	Btcache           *p = btcache_head;
	Btcache           *q = NULL;
	int            count = 0;
	int       used_count = 0;
	int      slice_count = piece_length / (16*1024);

	if(base_count < 0)  return -1;

	while(p != NULL) 
	{
		if(count % slice_count == 0)  
		{ 
			used_count = 0; 
			q = p; 
		}
		if(p->in_use==1 && p->read_write==0)  
			used_count += p->access_count;
		if(used_count == base_count)  break;  // 找到一个空闲的piece
		
		count++;
		p = p->next;
	}

	if(p != NULL) 
	{
		p = q;
		while(slice_count > 0) 
		{
			p->index  = -1;
			p->begin  = -1;
			p->length = -1;
			
			p->in_use       =  0;
			p->read_write   = -1;
			p->is_full      =  0;
			p->is_writed    =  0;
			p->access_count =  0;

			slice_count--;
			p = p->next;
		}
	}

	return 0;
}

// 下载完一个slice后,检查是否该slice为一个piece最后一块
// 若是则写入硬盘,只对刚刚开始下载时起作用,这样可以立即使peer得知
int is_a_complete_piece(int index, int *sequnce)
{
	Btcache          *p = btcache_head;
	int     slice_count = piece_length / (16*1024);
	int           count = 0;
	int             num = 0;
	int        complete = 0;

	while(p != NULL) 
	{
		if( count%slice_count==0 && p->index!=index ) 
		{
			num = slice_count;
			while(num>0 && p!=NULL)  
			{ 
				p = p->next; 
				num--; 
				count++; 
			}
			continue;
		}
		if( count%slice_count!=0 || p->read_write!=1 || p->is_full!=1) 
			break;

		*sequnce = count;
		num = slice_count;
	
		while(num>0 && p!=NULL) 
		{
			if(p->index==index && p->read_write==1 && p->is_full==1)
				 complete++;
			else break;
	
			num--;
			p = p->next;
		}

		break;
	}

	if(complete == slice_count) return 1;
	else return 0;
}

// 将16MB的缓冲区中所存的所有数据清空
void clear_btcache()
{
	Btcache *node = btcache_head;
	while(node != NULL)
	{
		node->index  = -1;
		node->begin  = -1;
		node->length = -1;
		
		node->in_use       =  0;
		node->read_write   = -1;
		node->is_full      =  0;
		node->is_writed    =  0;
		node->access_count =  0;
		
		node = node->next;
	}
}

// 将从peer处获取的一个slice存储到缓冲区中
int write_slice_to_btcache(int index,int begin,int length,
						   unsigned char *buff,int len,Peer *peer)
{
	int     count = 0, slice_count, unuse_count;
	Btcache *p = btcache_head, *q = NULL;  // q指向每个piece第一个slice
	
	if(p == NULL)  return -1;
	if(index>=pieces_length/20 || begin>piece_length-16*1024)  return -1;
	if(buff==NULL || peer==NULL)  return -1;

	if(index == last_piece_index) 
	{
		write_slice_to_last_piece(index,begin,length,buff,len,peer);
		return 0;
	}

	if(end_mode == 1) 
	{
		if( get_bit_value(bitmap,index) == 1 )  
			return 0;
	}
	
	// 遍历缓冲区,检查当前slice所在的piece的其他数据是否已存在
	// 若存在说明不是一个新的piece,若不存在说明是一个新的piece
	slice_count = piece_length / (16*1024);
	while(p != NULL) 
	{
		if(count%slice_count == 0)  q = p;
		if(p->index==index && p->in_use==1)  
			break;

		count++;
		p = p->next;
	}
	
	// p非空说明当前slice所在的piece的有些数据已经下载
	if(p != NULL) 
	{
		count = begin / (16*1024);  // count存放当前要存的slice在piece中的索引
		p = q;
		while(count > 0)  { p = p->next; count--; }
		
		if(p->begin==begin && p->in_use==1 && p->read_write==1 && p->is_full==1)
			return 0; // 该slice已存在
		
		p->index  = index;
		p->begin  = begin;
		p->length = length;
		
		p->in_use       = 1;
		p->read_write   = 1;
		p->is_full      = 1;
		p->is_writed    = 0;
		p->access_count = 0;
		
		memcpy(p->buff,buff,len);
		printf("+++++ write a slice to btcache index:%-6d begin:%-6x +++++\n",
			   index,begin);
		
		// 如果是刚刚开始下载(下载到的piece不足10个),则立即写入硬盘,并告知peer
		if(download_piece_num < 1000) 
		{
			int sequece;
			int ret;
			ret = is_a_complete_piece(index,&sequece);
			if(ret == 1) 
			{
				printf("###### begin write a piece to harddisk ######\n");
				write_piece_to_harddisk(sequece,peer);
				printf("###### end   write a piece to harddisk ######\n");
			}
		}
		return 0;
	}
	
	// p为空说明当前slice是其所在的piece的第一块下载到的数据
	// 首先判断是否存在空的缓冲区,若不存在,则将已下载的写入硬盘
	int i = 4;
	while(i > 0) 
	{
		slice_count = piece_length / (16*1024);
		count       = 0;  // 计数当前指向第几个slice
		unuse_count = 0;  // 计数当前piece中有多少个空的slice
		Btcache *q;       
		p = btcache_head;
		
		while(p != NULL) 
		{
			if(count%slice_count == 0)  
			 { 
				 unuse_count = 0; 
				 q = p; 
			 }
			if(p->in_use == 0) unuse_count++;
			if(unuse_count == slice_count)  
				break;  // 找到一个空闲的piece
			
			count++;
			p = p->next;
		}
		
		if(p != NULL) 
		{
			p = q;
			count = begin / (16*1024);
			while(count > 0)  { p = p->next; count--; }

			p->index  = index;
			p->begin  = begin;
			p->length = length;
			
			p->in_use       = 1;
			p->read_write   = 1;
			p->is_full      = 1;
			p->is_writed    = 0;
			p->access_count = 0;
			
			memcpy(p->buff,buff,len);
			printf("+++++ write a slice to btcache index:%-6d begin:%-6x +++++\n",
				   index,begin);
			return 0;
		}
		
		if(i == 4) write_btcache_to_harddisk(peer);
		if(i == 3) release_read_btcache_node(16);
		if(i == 2) release_read_btcache_node(8);
		if(i == 1) release_read_btcache_node(0);
		i--;
	}
	
	// 如果还没有空闲的缓冲区,丢弃下载到这个slice
	printf("+++++ write a slice to btcache FAILED :NO BUFFER +++++\n");
	clear_btcache();

	return 0;
}

// 从缓冲区获取一个slice,读取的slice存放到buff指向的数组中
// 若缓冲区中不存在该slice,则从硬盘读slice所在的piece到缓冲区中
int read_slice_for_send(int index,int begin,int length,Peer *peer)
{
	Btcache  *p = btcache_head, *q;  // q指向每个piece第一个slice
	int       ret;
	
	// 检查参数是否有误
	if(index>=pieces_length/20 || begin>piece_length-16*1024)  return -1;

	ret = get_bit_value(bitmap,index);
	if(ret == 0)  
	{ 
		printf("peer requested slice did not download\n"); 
		return -1; 
	}

	if(index == last_piece_index) 
	{
		read_slice_for_send_last_piece(index,begin,length,peer);
		return 0;
	}

	// 待获取得slice缓冲区中已存在
	while(p != NULL) 
	{
		if(p->index==index && p->begin==begin && p->length==length &&
		       p->in_use==1 && p->is_full==1) 
		{
			// 构造piece消息
			ret = create_piece_msg(index,begin,p->buff,p->length,peer);
			if(ret < 0) 
			{ 
				printf("Function create piece msg error\n"); 
				return -1; 
			}
			p->access_count = 1;
			return 0;
		}
		p = p->next;
	}

	int i = 4, count, slice_count, unuse_count;
	while(i > 0) 
	{
		slice_count = piece_length / (16*1024);
		count = 0;  // 计数当前指向第几个slice
		p = btcache_head;

		while(p != NULL) 
		{
			if(count%slice_count == 0)  
			{ 
				unuse_count = 0; 
				q = p; 
			}
			if(p->in_use == 0) unuse_count++;
			if(unuse_count == slice_count)  
				break;  // 找到一个空闲的piece
			
			count++;
			p = p->next;
		}
		
		if(p != NULL) 
		{
			read_piece_from_harddisk(q,index);

			p = q;
			while(p != NULL) 
			{
				if(p->index==index && p->begin==begin && p->length==length &&
					p->in_use==1 && p->is_full==1) 
				{
					// 构造piece消息
					ret = create_piece_msg(index,begin,p->buff,p->length,peer);
					if(ret < 0) 
					{ 
						printf("Function create piece msg error\n"); 
						return -1; 
					}
					p->access_count = 1;
					return 0;
				}
				p = p->next;
			}
		}
		
		if(i == 4) write_btcache_to_harddisk(peer);
		if(i == 3) release_read_btcache_node(16);
		if(i == 2) release_read_btcache_node(8);
		if(i == 1) release_read_btcache_node(0);
		i--;
	}

	// 如果实在没有缓冲区了,就不读slice所在的piece到缓冲区中
	p = initialize_btcache_node();
	if(p == NULL)  
	{ 
		printf("%s:%d allocate memory error",__FILE__,__LINE__); 
		return -1; 
	}
	p->index  = index;
	p->begin  = begin;
	p->length = length;
	read_slice_from_harddisk(p);
	// 构造piece消息
	ret = create_piece_msg(index,begin,p->buff,p->length,peer);
	if(ret < 0) 
	{ 
		printf("Function create piece msg error\n"); return -1; 
	}
	// 释放刚刚申请的内存
	if(p->buff != NULL)  free(p->buff);
	if(p != NULL) free(p);

	return 0;
}

void clear_btcache_before_peer_close(Peer *peer)
{
	Request_piece  *req = peer->Request_piece_head;
	int			   i = 0, index[2] = {-1, -1};

	if(req == NULL)  return;
	while(req != NULL && i < 2) 
	{
		if(req->index != index[i]) 
		{ 
			index[i] = req->index; 
			i++; 
		}
		req = req->next;
	}

	Btcache *p = btcache_head;
	while( p != NULL ) {
		if( p->index != -1 && (p->index==index[0] || p->index==index[1]) ) {
			p->index  = -1;
			p->begin  = -1;
			p->length = -1;
			
			p->in_use       =  0;
			p->read_write   = -1;
			p->is_full      =  0;
			p->is_writed    =  0;
			p->access_count =  0;
		}
		p = p->next;
	}
}


// 针对下载最后一个piece的问题,修改以下几处：
// 在data.c头部增加了几个全局变量
// 在data.c中修改了初始分配动态内存函数和最终释放动态内存的函数
// 在rate.c中修改了create_req_slice_msg函数
// 在data.c中增加了以下4个函数
int write_last_piece_to_btcache(Peer *peer)
{
	int            index = last_piece_index, i;
	unsigned char  piece_hash1[20], piece_hash2[20];
	Btcache        *p = last_piece;

	// 校验piece的HASH值
	SHA1_CTX ctx;
	SHA1Init(&ctx);
	while(p != NULL) 
	{
		SHA1Update(&ctx,p->buff,p->length);
		p = p->next;
	}
	SHA1Final(piece_hash1,&ctx);
	
	for(i = 0; i < 20; i++)  piece_hash2[i] = pieces[index*20+i];

	if(memcmp(piece_hash1,piece_hash2,20) == 0) 
	{
		printf("@@@@@@  last piece downlaod OK @@@@@@\n");
	} 
	else 
	{
		printf("@@@@@@  last piece downlaod NOT OK @@@@@@\n");
		return -1;
	}

	p = last_piece;
	while( p != NULL) 
	{
		write_btcache_node_to_harddisk(p);
		p = p->next;
	}
	printf("@@@@@@  last piece write to harddisk OK @@@@@@\n");

	// 在peer中的请求队列中删除piece请求

	// 更新位图
	set_bit_value(bitmap,index,1);
	
	// 准备发送have消息
	for(i = 0; i < 64; i++) 
	{
		if(have_piece_index[i] == -1) 
	    { 
			have_piece_index[i] = index; 
			break; 
		}
	}

	download_piece_num++;
	if(download_piece_num % 10 == 0)  restore_bitmap();

	return 0;
}

int write_slice_to_last_piece(int index,int begin,int length,
							  unsigned char *buff,int len,Peer *peer)
{
	if(index != last_piece_index || begin > (last_piece_count-1)*16*1024)
		return -1;
	if(buff==NULL || peer==NULL)  return -1;

	// 定位到要写入哪个slice
	int count = begin / (16*1024);
	Btcache *p = last_piece;
	while(p != NULL && count > 0) 
	{
		count--;
		p = p->next;
	}

	if(p->begin==begin && p->in_use==1 && p->is_full==1)
		return 0; // 该slice已存在
	
	p->index  = index;
	p->begin  = begin;
	p->length = length;

	p->in_use       = 1;
	p->read_write   = 1;
	p->is_full      = 1;
	p->is_writed    = 0;
	p->access_count = 0;
	
	memcpy(p->buff,buff,len);

	p = last_piece;
	while(p != NULL) 
	{
		if(p->is_full != 1)  break;
		p = p->next;
	}
	if(p == NULL) 
	{
		write_last_piece_to_btcache(peer);
	}

	return 0;
}

int read_last_piece_from_harddisk(Btcache *p, int index)
{
	Btcache  *node_ptr   = p;
	int      begin       = 0;
	int      length      = 16*1024;
	int      slice_count = last_piece_count; 
	int      ret;
	
	if(p==NULL || index != last_piece_index)  return -1;
	
	while(slice_count > 0) 
	{
		node_ptr->index  = index;
		node_ptr->begin  = begin;
		node_ptr->length = length;
		if(begin == (last_piece_count-1)*16*1024) 
		node_ptr->length = last_slice_len;
		
		ret = read_slice_from_harddisk(node_ptr);
		if(ret < 0) 
			return -1;
		
		node_ptr->in_use       = 1;
		node_ptr->read_write   = 0;
		node_ptr->is_full      = 1;
		node_ptr->is_writed    = 0;
		node_ptr->access_count = 0;
		
		begin += 16*1024;
		slice_count--;
		node_ptr = node_ptr->next;
	}
	
	return 0;
}

int read_slice_for_send_last_piece(int index,int begin,int length,Peer *peer)
{
	Btcache  *p;
	int       ret, count = begin / (16*1024);
	
	// 检查参数是否有误
	if(index != last_piece_index || begin > (last_piece_count-1)*16*1024)
		return -1;
	
	ret = get_bit_value(bitmap,index);
	if(ret <= 0)  
	{
		printf("peer requested slice did not download\n"); 
		return -1;
	}

	p = last_piece;
	while(count > 0) 
	{
		p = p->next;
		count --;
	}
	if(p->is_full != 1) 
	{
		ret = read_last_piece_from_harddisk(last_piece,index);
		if(ret < 0)  
			return -1;
	}
	
	if(p->in_use == 1 && p->is_full == 1) 
	{
		ret = create_piece_msg(index,begin,p->buff,p->length,peer);
	}

	if(ret == 0)  
		return 0;
	else 
		return -1;
}
