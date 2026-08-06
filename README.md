# ESP32 Stock Clock (M5StickS3)

PlatformIO + Arduino + M5Unified。主菜单进入 Clock / Stock / 配置。

## 功能

1. **Home**：Clock / Stock / Setting 三项；**A 进入**，**B 下一项**
2. **Clock**：大号时间（局部刷新，不闪屏）+ 日期 + IP 城市天气
3. **Stock**：单只股票；**A 下一只**，**B 切换视图**（价格 / 涨跌 / 详情）
4. **配置**：屏上显示 SoftAP/STA 信息；浏览器改 WiFi、增删股票
5. **长按 A 或 B**：从功能页返回主菜单

## 构建 / 烧录

```bash
pio run -e m5sticks3
pio run -e m5sticks3 -t upload
pio device monitor
```

烧录：长按侧边复位约 2 秒至绿灯闪烁；或关机后按住 BtnA 再插 USB。

## 配置页（Web）

| 项 | 值 |
|----|-----|
| SoftAP SSID | `M5StickS3-Clock` |
| SoftAP 密码 | `12345678` |
| 页面 | `http://192.168.4.1`（连上 STA 后也可用设备 IP） |

可修改 WiFi；股票代码示例：`s_sh000001` / `s_usNDX` / `s_usAAPL`。

## 按键

| 场景 | A | B | 长按 |
|------|---|---|------|
| 主菜单 | 进入 | 下一项 | — |
| Clock | — | — | 回主菜单 |
| Stock | 下一只股票 | 切换视图 | 回主菜单 |
| 配置 | — | — | 回主菜单 |

## 常见问题

- **串口无输出**：需 `-DARDUINO_USB_CDC_ON_BOOT=1`（已配置）
- **烧录端口**：侧边复位进下载模式，或关机后按住 BtnA 插 USB
- **电量**：`M5.Power.getBatteryLevel()`；长按电源键约 6 秒硬件关机
