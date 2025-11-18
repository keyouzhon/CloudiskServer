# C++ 客户端（进行中）

该目录包含使用现代 C++17 重写的客户端雏形，当前目标是逐步替换原有 `client/` 下的 C 实现。

## 当前功能
- TCP 会话与 `Train_t` 协议封装
- 账号注册 / 登录（与服务器兼容）
- 简易命令循环：支持 `ls`、`exit`
- RAII 和 `snprintf/std::string` 替换绝大部分不安全 API

> 注意：上传/下载、多点下载等仍未迁移，默认使用原 C 客户端完成。后续会在此基础上实现。

## 编译
```bash
cd client_cpp
make
```
生成的可执行文件为 `process_client_cpp`。

## 运行
```bash
./process_client_cpp <server_ip> <server_port>
```

## 下一步规划
1. 引入 `TransferManager`，迁移普通上传/下载
2. 迁移多点下载（VIP）
3. 统一配置、日志、token 重连等能力
4. 清理遗留的 C 客户端（完成全部功能后）

欢迎在 `client_cpp` 中继续扩展，实现完整替代。

