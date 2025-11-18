-- ============================================
-- CloudiskServer 数据库初始化脚本
-- 数据库名: Netdisk
-- ============================================

-- 创建数据库
CREATE DATABASE IF NOT EXISTS Netdisk DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

USE Netdisk;

-- ============================================
-- 1. User 表 - 用户信息表
-- ============================================
CREATE TABLE IF NOT EXISTS User (
    name VARCHAR(30) PRIMARY KEY COMMENT '用户名（主键）',
    salt VARCHAR(20) NOT NULL COMMENT '密码加密盐值',
    ciphertext VARCHAR(100) NOT NULL COMMENT '加密后的密码',
    token VARCHAR(50) DEFAULT NULL COMMENT '登录令牌，用于断线重连',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='用户表';

-- ============================================
-- 2. Directory 表 - 文件目录表（树形结构）
-- ============================================
CREATE TABLE IF NOT EXISTS Directory (
    code INT AUTO_INCREMENT PRIMARY KEY COMMENT '目录/文件唯一标识（自增主键）',
    procode INT NOT NULL DEFAULT 0 COMMENT '父目录code，0表示根目录',
    filename VARCHAR(30) NOT NULL COMMENT '文件名或目录名',
    belongID VARCHAR(30) NOT NULL COMMENT '所属用户（用户名）',
    filetype CHAR(1) NOT NULL COMMENT '类型：d=目录，f=文件',
    md5sum VARCHAR(50) DEFAULT NULL COMMENT '文件MD5值（用于去重和秒传）',
    filesize INT DEFAULT 0 COMMENT '文件大小（字节），目录为0',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    INDEX idx_procode (procode) COMMENT '父目录索引',
    INDEX idx_belongID (belongID) COMMENT '用户索引',
    INDEX idx_md5sum (md5sum) COMMENT 'MD5索引（用于秒传查询）',
    INDEX idx_procode_belongID (procode, belongID) COMMENT '复合索引（常用查询）',
    FOREIGN KEY (belongID) REFERENCES User(name) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='文件目录表';

-- ============================================
-- 3. Login 表 - 登录日志表
-- ============================================
CREATE TABLE IF NOT EXISTS Login (
    id INT AUTO_INCREMENT PRIMARY KEY COMMENT '日志ID（自增主键）',
    action VARCHAR(50) NOT NULL COMMENT '操作类型（如：Connect, Login request, Token request等）',
    name VARCHAR(30) DEFAULT '---' COMMENT '用户名',
    ip_port VARCHAR(100) DEFAULT '---' COMMENT 'IP地址和端口',
    result VARCHAR(50) DEFAULT '---' COMMENT '操作结果（Success, False等）',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '操作时间',
    INDEX idx_name (name) COMMENT '用户名索引',
    INDEX idx_created_at (created_at) COMMENT '时间索引'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='登录日志表';

-- ============================================
-- 4. Operate 表 - 操作日志表
-- ============================================
CREATE TABLE IF NOT EXISTS Operate (
    id INT AUTO_INCREMENT PRIMARY KEY COMMENT '日志ID（自增主键）',
    name VARCHAR(30) NOT NULL COMMENT '用户名',
    handle VARCHAR(50) NOT NULL COMMENT '操作类型（如：Ls, Download, Upload, mkdir, cd, rm等）',
    object VARCHAR(200) DEFAULT '---' COMMENT '操作对象（文件名、路径等）',
    result VARCHAR(50) DEFAULT '---' COMMENT '操作结果（Success, False等）',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '操作时间',
    INDEX idx_name (name) COMMENT '用户名索引',
    INDEX idx_handle (handle) COMMENT '操作类型索引',
    INDEX idx_created_at (created_at) COMMENT '时间索引'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='操作日志表';

-- ============================================
-- 5. Server_info 表 - 多点下载服务器信息表
-- ============================================
CREATE TABLE IF NOT EXISTS Server_info (
    id INT AUTO_INCREMENT PRIMARY KEY COMMENT '记录ID（自增主键）',
    md5sum VARCHAR(50) NOT NULL COMMENT '文件MD5值',
    ip VARCHAR(20) NOT NULL COMMENT '服务器IP地址',
    port INT NOT NULL COMMENT '服务器端口号',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    INDEX idx_md5sum (md5sum) COMMENT 'MD5索引（用于查询文件对应的服务器）',
    INDEX idx_ip_port (ip, port) COMMENT '服务器索引'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='多点下载服务器信息表';

-- ============================================
-- 初始化数据（可选）
-- ============================================

-- 如果需要，可以在这里插入一些测试数据
-- 例如：插入一个测试用户（密码需要先加密）
-- INSERT INTO User (name, salt, ciphertext) VALUES ('test', '$5$salt123', 'encrypted_password');

-- ============================================
-- 查看表结构
-- ============================================
-- SHOW TABLES;
-- DESCRIBE User;
-- DESCRIBE Directory;
-- DESCRIBE Login;
-- DESCRIBE Operate;
-- DESCRIBE Server_info;

