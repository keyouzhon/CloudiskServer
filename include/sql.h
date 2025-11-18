#ifndef __SQL_H__
#define __SQL_H__
#include "head.h"
#include "ser_cli.h"
#define STR_LEN 10

// 定义链表节点结构，存储路径信息
typedef struct node
{
    char path[30];          // 路径名
    struct node *next;      // 指向下一个节点的指针 
}node;

// 连接数据库，返回连接句柄
int sql_connect(MYSQL **conn);

// 查找用户名对应的盐值，找不到返回-1，否则返回0并将salt存入形参
int find_name(MYSQL *conn,char *name,char*);

// 生成随机盐值
void get_salt(char *str);

// 添加新用户到数据库
void add_user(MYSQL *conn,char *name,char *salt,char *mima);

// 匹配用户名和密码，成功则更新token
int math_user(MYSQL *conn,char *name,char *password,char *token);

// 匹配用户名和token
int math_token(MYSQL *conn,char *name,char *token);

// 实现ls功能，获取目录文件列表
void ls_func(MYSQL *conn,char*name,int code,char *buf);

// 文件与数据库操作主函数
int operate_func(MYSQL *conn,Train_t *ptrain,QUR_msg *pqq_msg,char *name,int *pcode);

// 查找指定路径和父目录code对应的code
int find_pre_code(MYSQL *conn,char*path,int pcode);

// 通过当前code、文件名和用户名查找下一级目录或文件的code
int find_later_code(MYSQL *conn,int cur_code,char *filename,char *name);

// 通过当前code、文件名和用户名查找下一级文件
int find_later_file(MYSQL *conn,int cur_code,char *filename,char *name);

// cd命令功能实现函数
int cd_func(MYSQL *conn,Train_t *ptrain,QUR_msg *pqq_msg,char *name,int *pcode);

// 删除文件或目录
int delete_file(MYSQL *conn,int code,char *name);

// 查找某个文件的信息，存入File_info结构体
int find_file_info(MYSQL *conn,File_info*,char*name,int code);

// 判断文件是否已经上传，避免重复上传
int math_uoload(MYSQL *conn,File_info *pfile_info,char*name,int code);

// 将文件信息添加到数据库
void add_file(int code,char *name,File_info *pf);

// 登录日志记录
void Llog(MYSQL *conn,const char *action,const char *name,const char *ip,const char *result);

// 操作日志记录
void Olog(MYSQL *conn,const char *action,const char *name,const char *ip,const char *result);

// 获取消息队列缓冲区
int get_mqbuf(MYSQL *conn,MQ_buf *pf);

#endif
