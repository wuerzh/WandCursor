# WandCursor

一款 Windows 上的魔杖式鼠标指示器。杖尖就是鼠标热点，红色杖身沿 45° 斜向延伸，用来做演示、教学、录屏时的光标强调。

![WandCursor 产品效果图](assets/effect.png)

## 特点

- **杖尖即光标**：象牙色尖端精确落在系统报告的鼠标位置，不改变你的点击目标。
- **不遮挡内容**：没有光圈、辉光、星芒或大面积高亮，按钮和文字仍然清楚可见。
- **点击穿透**：使用透明置顶覆盖层，鼠标点击会传递给下方窗口。
- **DPI 稳定尺寸**：优先启用 Per-Monitor DPI Awareness V2，Windows 显示缩放变化时，魔杖大小由 `WAND_SCALE` 控制，不随系统缩放一起变大变小。
- **纯 GDI+ 矢量绘制**：没有位图素材，使用渐变和路径绘制红色杖身、金色箍环、金色杖头与尖锐象牙杖尖。
- **单文件运行**：Release 中的 `WandCursor.exe` 约 281 KB，不需要安装器。

## 快捷键

| 快捷键 | 功能 |
| --- | --- |
| `Ctrl` + `Alt` + `Z` | 显示 / 隐藏魔杖 |
| `Ctrl` + `Alt` + `X` | 退出程序 |

## 使用方法

1. 双击 `WandCursor.exe`。
2. 移动鼠标，魔杖会跟随鼠标，杖尖保持在点击位置。
3. 按 `Ctrl+Alt+Z` 可以临时隐藏，再按一次恢复。
4. 按 `Ctrl+Alt+X` 退出。

程序不会显示任务栏按钮或托盘图标，这是为了避免录屏和演示时干扰画面。

## 调整大小

在 [WandCursor.c](WandCursor.c) 中修改：

```c
#define WAND_SCALE      1.5
```

例如：

- `1.25`：更克制
- `1.5`：当前默认大小
- `1.75`：更醒目

修改后重新构建即可。

## 构建

需要安装 [MinGW-w64](https://www.mingw-w64.org/) 并确保可以使用 `g++`。

PowerShell：

```powershell
.\build.ps1
```

或者直接编译：

```powershell
g++ WandCursor.c -o WandCursor.exe `
    -municode -mwindows `
    -static -static-libgcc -static-libstdc++ `
    -lgdiplus -lgdi32 -luser32 -O2 -Wall
```

说明：

- 源文件扩展名是 `.c`，但使用了 GDI+ 的 C++ API，需要用 `g++` 编译。
- `-municode` 对应入口函数 `wWinMain`。
- `-mwindows` 生成无控制台窗口的 GUI 程序。
- `-static -static-libgcc -static-libstdc++` 用于生成自包含的可执行文件。

## 文件说明

| 文件 | 说明 |
| --- | --- |
| `WandCursor.c` | 主程序源码 |
| `build.ps1` | PowerShell 构建脚本 |
| `assets/effect.png` | README 使用的产品效果图 |
| `wand_preview.html` | 魔杖外观预览页，用于调配色与几何 |
| `LICENSE` | MIT 许可证 |

## 已知限制

- 如果 `Ctrl+Alt+Z` 或 `Ctrl+Alt+X` 已被其他软件占用，程序可能启动后直接退出。可以释放快捷键，或在源码中改键后重新构建。
- 在不支持 Per-Monitor DPI Awareness V2 的旧版 Windows 上，程序会回退到系统级 DPI 感知，显示缩放变化时可能仍有兼容性缩放影响。
- 可执行文件未签名，首次运行时 Windows SmartScreen 可能提示风险。

## 发布建议

如果手动上传到 GitHub，建议至少包含：

- `README.md`
- `WandCursor.c`
- `build.ps1`
- `assets/effect.png`
- `wand_preview.html`
- `LICENSE`
- `.gitignore`

`WandCursor.exe` 建议不要提交进仓库，单独上传到 GitHub Releases。

## 许可证

[MIT](LICENSE)
