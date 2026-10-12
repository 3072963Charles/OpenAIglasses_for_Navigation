// ===== 摄像头传感器型号检测 — 纯 I2C，不连 WiFi =====
// 烧录后打开串口监视器 115200 baud

#include <Wire.h>

// XIAO ESP32S3 Sense 摄像头 SCCB 引脚
#define SIOD_PIN 40
#define SIOC_PIN 39

// ---- OV 系列通用: 读 8-bit 寄存器 ----
uint8_t ov_read8(uint8_t i2c_addr, uint8_t reg) {
  Wire.beginTransmission(i2c_addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  Wire.requestFrom(i2c_addr, (uint8_t)1);
  if (!Wire.available()) return 0xFF;
  return Wire.read();
}

// ---- 某些传感器用 16-bit 寄存器 ----
uint8_t ov_read16(uint8_t i2c_addr, uint16_t reg) {
  Wire.beginTransmission(i2c_addr);
  Wire.write((uint8_t)(reg >> 8));   // 高字节
  Wire.write((uint8_t)(reg & 0xFF)); // 低字节
  if (Wire.endTransmission(false) != 0) return 0xFF;
  Wire.requestFrom(i2c_addr, (uint8_t)1);
  if (!Wire.available()) return 0xFF;
  return Wire.read();
}

void identify_sensor(uint8_t addr) {
  Serial.printf("\n--- 检测 I2C 地址 0x%02X ---\n", addr);

  // ---------------------------------------------------
  // 方法 1: OV2640 / OV7725 风格 (8-bit 寄存器)
  //          PID = 0x0A,  VER = 0x0B
  // ---------------------------------------------------
  uint8_t pid8  = ov_read8(addr, 0x0A);
  uint8_t ver8  = ov_read8(addr, 0x0B);
  uint8_t mid8  = ov_read8(addr, 0x1C);  // OV2640 MID
  Serial.printf("  8-bit PID=0x%02X  VER=0x%02X  MID=0x%02X\n", pid8, ver8, mid8);

  if (pid8 == 0x26 && ver8 == 0x42) {
    Serial.println("  ✅ 确认为 OV2640");
    return;
  }
  if (pid8 == 0x77 && ver8 == 0x21) {
    Serial.println("  ✅ 确认为 OV7725");
    return;
  }

  // ---------------------------------------------------
  // 方法 2: OV3660 / OV5640 风格 (16-bit 寄存器)
  //          PID = 0x300A, VER = 0x300B
  // ---------------------------------------------------
  uint8_t pid16a = ov_read16(addr, 0x300A);
  uint8_t pid16b = ov_read16(addr, 0x300B);
  Serial.printf("  16-bit PID(0x300A)=0x%02X  VER(0x300B)=0x%02X\n", pid16a, pid16b);

  if (pid16a == 0x36 && pid16b == 0x60) {
    Serial.println("  ✅ 确认为 OV3660");
    return;
  }
  if (pid16a == 0x56 && pid16b == 0x40) {
    Serial.println("  ✅ 确认为 OV5640");
    return;
  }

  // ---------------------------------------------------
  // 方法 3: GC 系列 (GalaxyCore) — GC032A / GC2145 / GC0308
  //          GC2145: CHIP_ID = 0xF0→0x21
  //          GC032A: CHIP_ID = 0xF0→0x9d, 0xF1→0x03
  // ---------------------------------------------------
  uint8_t gc_chip_hi = ov_read8(addr, 0xF0);
  uint8_t gc_chip_lo = ov_read8(addr, 0xF1);
  Serial.printf("  GC-CHIP ID: 0x%02X%02X\n", gc_chip_hi, gc_chip_lo);

  if (gc_chip_hi == 0x21 && gc_chip_lo == 0x45) {
    Serial.println("  ✅ 确认为 GC2145");
    return;
  }
  if (gc_chip_hi == 0x9d && gc_chip_lo == 0x03) {
    Serial.println("  ✅ 确认为 GC032A");
    return;
  }
  if (gc_chip_hi == 0x9d && gc_chip_lo == 0x08) {
    Serial.println("  ✅ 确认为 GC0308");
    return;
  }

  // ---------------------------------------------------
  // 方法 4: BF 系列 (ByD / BF3007 等)
  //          BF3007: PID(0xFC)→0x30, VER(0xFE)→0x07
  // ---------------------------------------------------
  uint8_t bf_pid = ov_read8(addr, 0xFC);
  uint8_t bf_ver = ov_read8(addr, 0xFE);
  Serial.printf("  BF-style PID=0x%02X  VER=0x%02X\n", bf_pid, bf_ver);
  if (bf_pid == 0x30 && bf_ver == 0x07) {
    Serial.println("  ✅ 确认为 BF3007");
    return;
  }

  // ---------------------------------------------------
  // 方法 5: SC 系列 (Smartsens)
  //          SC030IOT: PID(0x3107)→0x9A, VER(0x3108)→0x30
  //          SC101IOT: PID(0x3107)→0x9B
  // ---------------------------------------------------
  uint8_t sc_pid = ov_read16(addr, 0x3107);
  uint8_t sc_ver = ov_read16(addr, 0x3108);
  Serial.printf("  SC-style PID=0x%02X  VER=0x%02X\n", sc_pid, sc_ver);
  if (sc_pid == 0x9A) { Serial.println("  ✅ 确认为 SC030IOT"); return; }

  // ---------------------------------------------------
  // 未匹配
  // ---------------------------------------------------
  Serial.println("  ❓ 未知传感器型号");
  Serial.println("  → 请把以上所有 hex 值截图发给我，我来确认");
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\n┌──────────────────────────────────────────┐");
  Serial.println(  "│  Camera Sensor ID Detector               │");
  Serial.println(  "│  XIAO ESP32S3 Sense                      │");
  Serial.println(  "└──────────────────────────────────────────┘\n");

  // --- 1. I2C 扫描 ---
  Wire.begin(SIOD_PIN, SIOC_PIN);
  Wire.setClock(100000);

  Serial.println("正在扫描 I2C 总线...");
  uint8_t devices[10];
  int count = 0;

  for (uint8_t addr = 1; addr < 0x7F; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      devices[count++] = addr;
      Serial.printf("  发现设备: 0x%02X (%d)\n", addr, addr);
    }
  }

  if (count == 0) {
    Serial.println("\n❌ I2C 总线上没有任何设备！");
    Serial.println("   → 检查摄像头排线是否插好、是否插反");
    Serial.println("   → 检查排线 FPC 座是否有虚焊");
    while (1) delay(1000);
  }

  // --- 2. 逐个识别 ---
  Serial.printf("\n找到 %d 个设备，开始识别...\n", count);
  for (int i = 0; i < count; i++) {
    identify_sensor(devices[i]);
  }

  Serial.println("\n══════════════════════════");
  Serial.println("  检测完毕");
  Serial.println("══════════════════════════");
}

void loop() {
  delay(60000);
}
