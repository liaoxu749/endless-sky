# Endless Sky ESMP — 安卓版使用说明

## 一、拿到 APK（两种方式，选一）

### 方式 A：GitHub Actions 自动构建（推荐，无需电脑）

1. 在电脑或手机浏览器上：fork `endless-sky/endless-sky` 到你的 GitHub 账号；
2. 把本分支（`lan-multiplayer`）推上去。没有电脑时可以直接在 GitHub 网页端
   上传 5 个补丁：
   - 进入你的 fork → 分支 `master` → `Add file → Upload files` 不适用补丁，
     改用 GitHub Codespaces（网页版终端，免费额度够用）：
     ```bash
     git clone https://github.com/<你的用户名>/endless-sky.git
     cd endless-sky
     # 把 /sdcard/Download/DSH-Go/esmp-patches/*.patch 上传到 Codespaces
     # （拖拽到文件面板即可），然后：
     git am /path/to/esmp-patches/*.patch
     git push origin HEAD:lan-multiplayer
     ```
3. 推送后 GitHub 会自动跑 **Android APK** 工作流（约 40–80 分钟，首次较慢，
   因为要编译全部依赖库）；
4. 打开仓库 → **Actions** → 最近的运行 → **Artifacts** → 下载
   `endless-sky-esmp-apk`，解压得到 APK；
5. 手机上安装（允许安装未知来源应用）。

### 方式 B：自己电脑构建

按 `DESIGN.md` 的构建命令 + `android/` 目录用 `gradle assembleDebug` 打包。

## 二、首次启动

- 第一次打开会显示黑屏一段时间：**正在把约 500MB 的游戏资源从 APK 解包到
  内部存储**，属于正常现象，只发生一次；再次启动进入会很快。
- 若启动报错，先看 `errors.txt`（`android/data/org.endlesssky.esmp/` 或应用
  内部目录，视版本而定），反馈给开发者。

## 三、触屏操作

进入游戏（飞行画面）后，屏幕左右两侧有虚拟按键：

| 左列 | 作用 | 右列 | 作用 |
|------|------|------|------|
| FWD | 前进（按住） | FIRE | 开火（按住） |
| REV | 后退（按住） | JUMP | 跃迁（按住直到起跳） |
| L / R | 左转 / 右转（按住） | LAND | 降落 |
| | | BOARD | 登船 |
| | | MENU | 打开菜单 |

- 其余操作（点选目标、面板、商店）直接用手指点按——触屏会自动映射为鼠标；
- 侧边按键按住期间持续生效（如按住 FWD 就是持续推进）；
- 游戏内文字均为英文（游戏字体不含中文）。

## 四、局域网联机（手机 ↔ 手机 / 电脑）

1. 两台设备连入**同一 Wi-Fi**（或一台开热点）；
2. 主机端：主菜单按 **U** → 「Host」按钮（建房后照常进游戏）；
3. 客户端：主菜单按 **U** → 等待列表出现主机 → 选中 → **Join**；
4. 双方各自开始新游戏（默认起点相同），进入同一星系即可互相看到对方；
5. 联机细节与验证清单见 `DESIGN.md`。

提示：部分手机的省电策略会拦 UDP 广播，发现不了主机时可用直填 IP 的方式
（后续版本在大厅加输入框；当前版本可用电脑端当主机验证）。

## 五、已知限制

- 首启解包 500MB 资源需要等待；
- 触屏手感需要按真机反馈继续调；
- 主机暂停/快进不联动；伤害结算同步在 M2；
- 上下滑动等更多手势、 shops 里的滚动操作按需迭代。
