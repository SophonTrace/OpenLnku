# 构建与维护

## 构建与离线验证
需要 Windows x64、Visual Studio C++ 工作负载、Windows SDK 和 Qt 6.8.3 MSVC x64。发布脚本使用 PowerShell 7.4 或更新版本；本次使用 7.6.5 验证。Qt 默认位于 .deps/Qt/6.8.3/msvc2022_64，也可指定 QT_ROOT。VERSION.txt 是发布版本来源。

```powershell
./tools/build.cmd
./tools/package.ps1
./tools/verify-package.ps1
./tools/verify-update-handoff.ps1
./tools/installer.ps1
./build/OpenLnku.exe --demo
```

普通启动使用真实认证；--demo 只使用合成响应，不验证账号密码，不创建认证 UDP socket，不写真实开机启动项。--capture、--demo-connect、--settings-file、--test-identity 与预览参数仅允许离线模式，不能误触真实认证。

实网兼容验收与离线构建验收分开记录。OpenLnku 为独立项目。

更新交接验收使用工作区 GUID 目录中的无网络测试程序，分别模拟 1.2.0 和当前版本。覆盖助手延迟就绪、父进程校验与退出、文件替换后自动重启、EXE 替换后的回滚、取消时释放安装锁，以及已发布 1.2.0 的父进程先退出故障。测试采用隔离互斥锁与临时缓存，不访问真实账号、认证、设置或注册表。


## 更新包签名

更新包使用现有发布公钥对应的私钥签名。签名私钥仅保存在本机受保护目录，不得进入 Git 仓库或发布包。验证用户反馈的修复后，才生成签名清单并发布更新。
