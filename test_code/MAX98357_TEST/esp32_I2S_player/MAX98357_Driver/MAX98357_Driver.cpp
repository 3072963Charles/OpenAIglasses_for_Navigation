/*
 * MAX98357_Driver.cpp
 * MAX98357A I2S 音频功放驱动实现
 */

#include "MAX98357_Driver.h"
#include <driver/i2s.h>
#include <SD.h>

// ─── I2S 端口 ───────────────────────────────────────────
static const i2s_port_t I2S_PORT = I2S_NUM_0;

// ─── 初始化 ─────────────────────────────────────────────

bool MAX98357::init(int bclk, int lrc, int dout, int sdPin, int sampleRate) {
  _bclk       = bclk;
  _lrc        = lrc;
  _dout       = dout;
  _sdPin      = sdPin;
  _sampleRate = sampleRate;
  _volume     = 30;
  _playing    = false;
  _muted      = false;

  _ampOn();
  _initialized = true;
  return true;
}

// ─── 播放方波 ───────────────────────────────────────────

void MAX98357::playTone(int freqHz, int durationMs, int amplitude) {
  if (!_initialized) return;
  stop();

  _i2sInit(_sampleRate, 16, 2);  // 16bit stereo
  _playing = true;

  float phaseStep  = (float)freqHz / _sampleRate;
  float phase      = 0.0f;
  float amp        = constrain(amplitude, 0, 100) / 100.0f * 32767.0f;
  // 叠加音量控制
  amp *= constrain(_volume, 0, 100) / 100.0f;

  const int BUF_FRAMES = 256;
  int16_t buf[BUF_FRAMES * 2];
  size_t  bytesWritten;
  unsigned long start = millis();

  while (_playing) {
    // 检查超时
    if (durationMs > 0 && (millis() - start >= (unsigned long)durationMs)) {
      break;
    }

    for (int i = 0; i < BUF_FRAMES; i++) {
      // 方波: 前半个周期 +amp, 后半个周期 -amp
      int16_t sample = (phase < 0.5f) ? (int16_t)amp : (int16_t)(-amp);
      buf[i * 2]     = sample;
      buf[i * 2 + 1] = sample;
      phase += phaseStep;
      if (phase >= 1.0f) phase -= 1.0f;
    }

    i2s_write(I2S_PORT, buf, sizeof(buf), &bytesWritten, portMAX_DELAY);
  }

  _i2sDeinit();
  _playing = false;
}

// ─── 播放 WAV 文件 ──────────────────────────────────────

bool MAX98357::playFile(const char* path,
                         int sdCs, int sdSck, int sdMiso, int sdMosi) {
  if (!_initialized) return false;
  stop();

  // 初始化 SD 卡
  SPI.begin(sdSck, sdMiso, sdMosi, sdCs);
  if (!SD.begin(sdCs)) {
    Serial.println("[MAX98357] SD 卡初始化失败");
    return false;
  }

  File file = SD.open(path);
  if (!file) {
    Serial.printf("[MAX98357] 文件打开失败: %s\n", path);
    return false;
  }

  // ─── 解析 WAV 头 ───
  struct __attribute__((packed)) WavHdr {
    char     riff[4];
    uint32_t fileSize;
    char     wave[4];
    char     fmt[4];
    uint32_t fmtSize;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
  };

  WavHdr hdr;
  file.read((uint8_t*)&hdr, sizeof(hdr));

  if (memcmp(hdr.riff, "RIFF", 4) || memcmp(hdr.wave, "WAVE", 4)) {
    Serial.println("[MAX98357] 无效的 WAV 文件");
    file.close();
    return false;
  }
  if (hdr.audioFormat != 1) {
    Serial.printf("[MAX98357] 不支持的编码格式: %d\n", hdr.audioFormat);
    file.close();
    return false;
  }

  // ─── 查找 data chunk ───
  file.seek(12 + 8 + hdr.fmtSize);
  uint32_t dataSize = 0;
  while (file.available()) {
    char     id[4];
    uint32_t size;
    file.read((uint8_t*)id, 4);
    file.read((uint8_t*)&size, 4);
    if (memcmp(id, "data", 4) == 0) {
      dataSize = size;
      break;
    }
    file.seek(file.position() + size);
  }

  if (dataSize == 0) {
    Serial.println("[MAX98357] 未找到 data chunk");
    file.close();
    return false;
  }

  Serial.printf("[MAX98357] 播放: %s | %dHz %dbit %dch | %.1f秒\n",
                path, hdr.sampleRate, hdr.bitsPerSample, hdr.numChannels,
                (float)dataSize / hdr.byteRate);

  // ─── 初始化 I2S ───
  _i2sInit(hdr.sampleRate, (hdr.bitsPerSample == 8) ? 16 : hdr.bitsPerSample, hdr.numChannels);

  // ─── 播放循环 ───
  _playing = true;

  const int BUF_BYTES  = 2048;
  uint8_t   raw[BUF_BYTES];
  int16_t   out[BUF_BYTES];          // 16bit stereo 输出缓冲
  size_t    bytesWritten;
  uint32_t  remaining = dataSize;
  int       sampleW   = hdr.bitsPerSample / 8;
  bool      isStereo  = (hdr.numChannels == 2);

  while (_playing && remaining > 0) {
    int toRead = min((uint32_t)BUF_BYTES, remaining);
    int len    = file.read(raw, toRead);
    if (len <= 0) break;

    if (hdr.bitsPerSample == 8) {
      // ── 8bit 无符号 → 16bit 有符号 ──
      for (int i = 0; i < len; i++) {
        out[i] = ((int16_t)raw[i] - 128) << 8;
      }
      if (!isStereo) {
        // mono → stereo
        for (int i = len - 1; i >= 0; i--) {
          out[i * 2]     = out[i];
          out[i * 2 + 1] = out[i];
        }
        i2s_write(I2S_PORT, out, len * 4, &bytesWritten, portMAX_DELAY);
      } else {
        i2s_write(I2S_PORT, out, len * 2, &bytesWritten, portMAX_DELAY);
      }
    } else {
      // ── 16bit ──
      if (!isStereo) {
        int16_t* src = (int16_t*)raw;
        int n = len / 2;
        for (int i = n - 1; i >= 0; i--) {
          out[i * 2]     = src[i];
          out[i * 2 + 1] = src[i];
        }
        i2s_write(I2S_PORT, out, n * 4, &bytesWritten, portMAX_DELAY);
      } else {
        i2s_write(I2S_PORT, raw, len, &bytesWritten, portMAX_DELAY);
      }
    }
    remaining -= len;
  }

  // ─── 清理 ───
  file.close();
  _i2sDeinit();
  _playing = false;
  return true;
}

// ─── 控制 ───────────────────────────────────────────────

void MAX98357::stop() {
  _playing = false;
  i2s_zero_dma_buffer(I2S_PORT);
}

void MAX98357::setVolume(int volume) {
  _volume = constrain(volume, 0, 100);
}

void MAX98357::mute() {
  if (_sdPin >= 0) {
    digitalWrite(_sdPin, LOW);
    _muted = true;
  }
}

void MAX98357::unmute() {
  if (_sdPin >= 0) {
    digitalWrite(_sdPin, HIGH);
    _muted = false;
  }
}

// ─── 内部函数 ───────────────────────────────────────────

void MAX98357::_i2sInit(int sampleRate, int bps, int channels) {
  i2s_bits_per_sample_t i2sBps = I2S_BITS_PER_SAMPLE_16BIT;

  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = (uint32_t)sampleRate,
    .bits_per_sample = i2sBps,
    .channel_format = (channels == 2) ? I2S_CHANNEL_FMT_RIGHT_LEFT : I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 512
  };

  i2s_pin_config_t pinCfg = {
    .bck_io_num = _bclk,
    .ws_io_num = _lrc,
    .data_out_num = _dout,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pinCfg);
  i2s_set_clk(I2S_PORT, sampleRate, i2sBps,
              (channels == 2) ? I2S_CHANNEL_STEREO : I2S_CHANNEL_MONO);
}

void MAX98357::_i2sDeinit() {
  i2s_stop(I2S_PORT);
  i2s_driver_uninstall(I2S_PORT);
}

void MAX98357::_ampOn() {
  if (_sdPin >= 0) {
    pinMode(_sdPin, OUTPUT);
    digitalWrite(_sdPin, HIGH);
  }
}

void MAX98357::_ampOff() {
  if (_sdPin >= 0) {
    digitalWrite(_sdPin, LOW);
  }
}

MAX98357::~MAX98357() {
  stop();
  _ampOff();
}

// ═══════════════════════════════════════════════════════
//  C 接口实现 (调用内部 C++ 单例)
// ═══════════════════════════════════════════════════════

static MAX98357 g_audio;   // 全局单例, 供 C 接口使用

extern "C" {

int max98357_init(int bclk, int lrc, int dout, int sd_pin, int sample_rate) {
  return g_audio.init(bclk, lrc, dout, sd_pin, sample_rate) ? 1 : 0;
}

void max98357_play_tone(int freq_hz, int duration_ms, int amplitude_pct) {
  g_audio.playTone(freq_hz, duration_ms, amplitude_pct);
}

int max98357_play_file(const char* path,
                       int sd_cs, int sd_sck, int sd_miso, int sd_mosi) {
  return g_audio.playFile(path, sd_cs, sd_sck, sd_miso, sd_mosi) ? 1 : 0;
}

void max98357_stop(void) {
  g_audio.stop();
}

void max98357_set_volume(int volume) {
  g_audio.setVolume(volume);
}

void max98357_mute(void) {
  g_audio.mute();
}

void max98357_unmute(void) {
  g_audio.unmute();
}

int max98357_is_playing(void) {
  return g_audio.isPlaying() ? 1 : 0;
}

} // extern "C"
