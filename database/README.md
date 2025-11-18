# CloudiskServer 数据库说明

## 数据库结构

本项目使用 MySQL 数据库，数据库名为 `Netdisk`。

### 表结构说明

#### 1. User 表 - 用户信息表
存储用户账号信息。

| 字段 | 类型 | 说明 |
|------|------|------|
| name | VARCHAR(30) | 用户名（主键） |
| salt | VARCHAR(20) | 密码加密盐值 |
| ciphertext | VARCHAR(100) | 加密后的密码 |
| token | VARCHAR(50) | 登录令牌（用于断线重连） |
| created_at | TIMESTAMP | 创建时间 |
| updated_at | TIMESTAMP | 更新时间 |

#### 2. Directory 表 - 文件目录表
存储用户的文件和目录结构（树形结构）。

| 字段 | 类型 | 说明 |
|------|------|------|
| code | INT | 目录/文件唯一标识（自增主键） |
| procode | INT | 父目录code，0表示根目录 |
| filename | VARCHAR(30) | 文件名或目录名 |
| belongID | VARCHAR(30) | 所属用户（用户名） |
| filetype | CHAR(1) | 类型：'d'=目录，'f'=文件 |
| md5sum | VARCHAR(50) | 文件MD5值（用于去重和秒传） |
| filesize | INT | 文件大小（字节），目录为0 |
| created_at | TIMESTAMP | 创建时间 |

**目录树结构说明：**
- `code=0` 表示根目录
- `procode` 指向父目录的 `code`
- 通过 `procode` 和 `code` 构建树形结构

#### 3. Login 表 - 登录日志表
记录用户登录、连接等操作日志。

| 字段 | 类型 | 说明 |
|------|------|------|
| id | INT | 日志ID（自增主键） |
| action | VARCHAR(50) | 操作类型（Connect, Login request, Token request等） |
| name | VARCHAR(30) | 用户名 |
| ip_port | VARCHAR(100) | IP地址和端口 |
| result | VARCHAR(50) | 操作结果（Success, False等） |
| created_at | TIMESTAMP | 操作时间 |

#### 4. Operate 表 - 操作日志表
记录用户文件操作日志。

| 字段 | 类型 | 说明 |
|------|------|------|
| id | INT | 日志ID（自增主键） |
| name | VARCHAR(30) | 用户名 |
| handle | VARCHAR(50) | 操作类型（Ls, Download, Upload, mkdir, cd, rm等） |
| object | VARCHAR(200) | 操作对象（文件名、路径等） |
| result | VARCHAR(50) | 操作结果（Success, False等） |
| created_at | TIMESTAMP | 操作时间 |

#### 5. Server_info 表 - 多点下载服务器信息表
存储多点下载的服务器节点信息。

| 字段 | 类型 | 说明 |
|------|------|------|
| id | INT | 记录ID（自增主键） |
| md5sum | VARCHAR(50) | 文件MD5值 |
| ip | VARCHAR(20) | 服务器IP地址 |
| port | INT | 服务器端口号 |
| created_at | TIMESTAMP | 创建时间 |

## 安装方法

### 方法1：使用安装脚本（推荐）

1. 修改 `install.sh` 中的数据库配置：
   ```bash
   DB_HOST="localhost"
   DB_USER="root"
   DB_PASS="950711"  # 修改为你的MySQL密码
   DB_NAME="Netdisk"
   ```

2. 给脚本添加执行权限：
   ```bash
   chmod +x install.sh
   ```

3. 执行安装脚本：
   ```bash
   ./install.sh
   ```

### 方法2：手动执行 SQL 脚本

1. 登录 MySQL：
   ```bash
   mysql -u root -p
   ```

2. 执行 SQL 脚本：
   ```bash
   mysql -u root -p < init.sql
   ```

   或者在 MySQL 命令行中：
   ```sql
   source /path/to/init.sql;
   ```

### 方法3：在 MySQL 客户端中执行

1. 打开 MySQL 客户端
2. 复制 `init.sql` 文件内容
3. 在 MySQL 客户端中粘贴并执行

## 验证安装

执行以下命令验证数据库是否创建成功：

```bash
mysql -u root -p -e "USE Netdisk; SHOW TABLES;"
```

应该看到以下5个表：
- User
- Directory
- Login
- Operate
- Server_info

查看表结构：
```bash
mysql -u root -p -e "USE Netdisk; DESCRIBE User;"
```

## 配置说明

### 修改数据库连接信息

如果数据库配置与代码中的默认值不同，需要修改 `src/sql.c` 文件：

```c
int sql_connect(MYSQL **conn)
{
    char server[]="localhost";    // 修改为你的数据库主机
    char user[]="root";           // 修改为你的数据库用户名
    char password[]="950711";     // 修改为你的数据库密码
    char database[]="Netdisk";    // 数据库名
    // ...
}
```

## 初始化数据

### 创建测试用户（可选）

数据库安装后，用户需要通过客户端注册账号。如果需要手动创建测试用户：

1. 生成盐值和加密密码（需要编写脚本或使用客户端）
2. 插入用户：
   ```sql
   INSERT INTO User (name, salt, ciphertext) 
   VALUES ('testuser', '$5$salt123', 'encrypted_password');
   ```

**注意：** 密码必须使用 `crypt()` 函数加密，建议通过客户端注册功能创建用户。

## 多点下载服务器配置

如果需要使用多点下载功能，需要在 `Server_info` 表中添加服务器信息：

```sql
INSERT INTO Server_info (md5sum, ip, port) 
VALUES ('file_md5_value', '192.168.1.100', 3000);
```

每个文件的 MD5 可以对应多个服务器节点（最多3个，由 `SPOT_NUM` 定义）。

## 常见问题

### 1. 权限错误
如果遇到权限错误，确保 MySQL 用户有创建数据库和表的权限：
```sql
GRANT ALL PRIVILEGES ON Netdisk.* TO 'root'@'localhost';
FLUSH PRIVILEGES;
```

### 2. 字符集问题
如果遇到中文乱码，确保数据库和表使用 `utf8mb4` 字符集（已在 SQL 脚本中设置）。

### 3. 外键约束
`Directory` 表有外键关联 `User` 表，删除用户时会自动删除该用户的所有文件和目录。

## 维护建议

1. **定期备份数据库**：
   ```bash
   mysqldump -u root -p Netdisk > backup.sql
   ```

2. **清理日志表**（可选）：
   ```sql
   DELETE FROM Login WHERE created_at < DATE_SUB(NOW(), INTERVAL 30 DAY);
   DELETE FROM Operate WHERE created_at < DATE_SUB(NOW(), INTERVAL 30 DAY);
   ```

3. **优化索引**：表结构已包含必要的索引，定期执行 `ANALYZE TABLE` 优化查询性能。

