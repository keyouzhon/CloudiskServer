# Enterprise Cloud Drive (C/S)

一个面向企业的 C/S 云盘原型，提供基于 TCP 的指令协议，支持用户注册、登录、目录浏览以及文件的上传、下载和删除功能。项目采用现代 C++20 实现，服务器端使用 SQLite 保存用户信息，客户端提供交互式命令行工具。

## 目录结构

- `common/`：客户端与服务器共享的网络工具、编码工具等头文件。
- `server/`：服务器源码、配置和默认存储目录。
- `client/`：交互式客户端。
- `data/`：默认 SQLite 数据库与日志输出目录。
- `server/storage/`：用户文件存储根目录。

## 功能特色

- **Reactor + epoll(LT)**：引入就绪事件链表和任务调度器，接入/收发/状态机全部在 Reactor 线程中完成，长耗时任务（MD5、mmap 拷贝等）由独立线程池异步回调，大幅减少阻塞。
- **自定义 TCP 协议**：所有消息以 12 字节二进制帧头 + k/v 头部 + 二进制 Body 组成，支持任意数据负载并保持粘包/半包友好。
- **Token 认证**：登录成功后发放 JWT Token，所有后续请求必须携带 Token，服务端逐帧校验，确保多终端同时在线也能安全鉴权。
- **云盘级目录管理**：支持 `pwd / cd / ls / mkdir / delete` 等指令，自动隔离用户根目录，禁止穿越到其他用户空间。
- **秒传 + 断点续传**：上传前比较客户端 MD5 与数据库记录，命中直接硬链接完成“秒传”；未命中时开启断点续传，上传进度落盘，断线重连即可继续。
- **大文件 mmap 优化**：当文件超过 100MB 时，上传端使用 `mmap` 读取、下载端使用 `mmap`/`pwrite` 写入，减少内核态/用户态来回复制。
- **安全密码存储**：使用 `crypt(3)` 的 SHA-512 加盐哈希，彻底替换旧的手写哈希逻辑；Token 使用 HMAC-SHA256 签名。

## 构建步骤

> 依赖：`cmake`, `g++ (>=11)`, `libsqlite3-dev`, `libssl-dev`

```bash
cd /home/hht/project/cloudDisk/enterprise_cloud_drive
cmake -S . -B build -DBUILD_CLIENT=ON -DBUILD_SERVER=ON
cmake --build build
```

生成物：

- `build/server/cloud_drive_server`
- `build/client/cloud_drive_client`

## 运行示例

1. **启动服务器**

```bash
./build/server/cloud_drive_server server/config/server.conf
```

2. **启动客户端并连接**

```bash
./build/client/cloud_drive_client 127.0.0.1 6000
```

3. **示例交互**

```
> register alice P@ssw0rd
OK Registered
> login alice P@ssw0rd
OK LoginSuccess
> upload ./report.pdf reports/report.pdf
OK UploadComplete
> list reports
LIST 1 report.pdf
> download reports/report.pdf ./report_local.pdf
OK DownloadComplete
> delete reports/report.pdf
OK Deleted
> logout
OK LoggedOut
> quit
BYE
```

## 协议概要

所有指令走自定义二进制帧：

```
[4B magic][2B version][2B header_len][4B body_len][header bytes][body bytes]
```

Header 为 `key=value` 的 ASCII 行，常见指令：

| 指令               | 描述                                    |
|-------------------|-----------------------------------------|
| `REGISTER`        | 注册账号，提交 `username/password`      |
| `LOGIN`           | 用户名密码登录，返回 `token`            |
| `TOKEN_AUTH`      | 使用已有 Token 复用会话                 |
| `DIR_PWD/LIST/MKDIR/CHANGE` | 目录管理指令                 |
| `FILE_UPLOAD_INIT/CHUNK/COMMIT` | 断点续传 & 秒传流程     |
| `FILE_DOWNLOAD_INIT/FETCH` | 按块拉取文件，支持续传        |
| `FILE_DELETE`     | 删除文件或目录                          |

所有非注册/登录指令必须携带 `token` 头，服务端逐条验证 JWT 以完成鉴权。

## 后续可扩展方向

- 引入更强的协议封装（如 gRPC/HTTP2）。
- 使用任务队列或线程池提升并发可扩展性。
- 为传输增加断点续传、分块校验、TLS 加密等高级特性。
- 开发图形化或移动客户端。
