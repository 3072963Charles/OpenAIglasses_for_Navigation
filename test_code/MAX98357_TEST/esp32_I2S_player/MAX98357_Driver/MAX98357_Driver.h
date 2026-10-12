/*
 * MAX98357_Driver.h
 *
 * MAX98357A I2S 音频功放驱动 — XIAO ESP32S3 Sense 专用
 *
 * 兼容 C 和 C++:
 *   C++ (.ino / .cpp):
 *       MAX98357 audio;
 *       audio.init(2, 1, 4, 43);
 *       audio.playTone(440, 2000);
 *
 *   C (.c):
 *       max98357_init(2, 1, 4, 43);
 *       max98357_play_tone(440, 2000);
 *
 * 功能:
 *   - I2S 初始化
 *   - 播放方波测试音 (指定频率/时长)
 *   - 播放 microSD 卡中的 WAV 文件
 *   - 音量控制 / 停止播放 / 静音
 */

#ifndef MAX98357_DRIVER_H
#define MAX98357_DRIVER_H

#include <Arduino.h>

#ifdef __cplusplus

// ============================================================
//  C++ 接口: 面向对象, 可创建多个实例
// ============================================================

class MAX98357 {
public:
  /*
   * 初始化功放模块
   *  bclk, lrc, dout : I2S 引脚 (GPIO)
   *  sdPin           : 使能引脚, -1 = 硬件接 3.3V 常开
   *  sampleRate      : 默认 44100
   */
  bool init(int bclk, int lrc, int dout, int sdPin = -1,
            int sampleRate = 44100);

  /* 播放方波 */
  void playTone(int freqHz, int durationMs = 500, int amplitude = 30);

  /* 播放 SD 卡 WAV 文件 */
  bool playFile(const char* path,
                int sdCs, int sdSck, int sdMiso, int sdMosi);

  void stop();
  void setVolume(int volume);       // 0 ~ 100
  void mute();
  void unmute();
  bool isPlaying() { return _playing; }

  ~MAX98357();

private:
  int _bclk, _lrc, _dout, _sdPin;
  int _sampleRate;
  int _volume;
  bool _initialized;
  bool _playing;
  bool _muted;

  void _i2sInit(int sampleRate, int bps, int channels);
  void _i2sDeinit();
  void _ampOn();
  void _ampOff();
};

extern "C" {
#endif // __cplusplus

// ============================================================
//  C 接口: 纯函数 API, 可与 C 文件混编
// ============================================================

/*
 * 注意: C 接口使用内部全局句柄, 只能创建一个设备实例.
 * 如需多实例, 请使用上方的 C++ class.
 */

/** 初始化功放模块, 返回 1=成功 0=失败 */
int max98357_init(int bclk, int lrc, int dout, int sd_pin, int sample_rate);

/** 播放指定频率方波, duration_ms 单位毫秒 */
void max98357_play_tone(int freq_hz, int duration_ms, int amplitude_pct);

/** 播放 SD 卡 WAV 文件, 返回 1=成功 0=失败 */
int max98357_play_file(const char* path,
                       int sd_cs, int sd_sck, int sd_miso, int sd_mosi);

/** 停止播放 */
void max98357_stop(void);

/** 设置音量 0~100 */
void max98357_set_volume(int volume);

/** 静音 / 解除静音 */
void max98357_mute(void);
void max98357_unmute(void);

/** 是否正在播放, 返回 1=播放中 0=空闲 */
int max98357_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif // MAX98357_DRIVER_H
