# FreeSWITCH Whisper 模块安装记录

## 安装环境
- FreeSWITCH 源码目录：/usr/src/freeswitch
- 时间：2025-02-09

## 安装步骤

### 1. 克隆 mod_whisper 仓库
```bash
cd /usr/src/freeswitch/src/mod/asr_tts
git clone https://github.com/cyrenity/mod_whisper.git
```

遇到问题：
- 错误：HTTP2 framing layer 错误
- 解决方案：尝试使用以下方法之一：
  1. 禁用 git 的 HTTP2
  ```bash
git config --global http.version HTTP/1.1
  ```
  2. 如果仍然失败，可以尝试下载 zip 包：
  ```bash
wget https://github.com/cyrenity/mod_whisper/archive/refs/heads/master.zip
unzip master.zip
mv mod_whisper-master mod_whisper
  ```

### 2. 安装依赖
首先需要安装 libwebsockets：
```bash
apt-get update
apt-get install -y libwebsockets-dev
```

### 3. 配置 FreeSWITCH 构建系统
1. 修改 configure.ac，添加 Makefile 配置
2. 添加 WebSocket 检查代码
3. 更新 modules.conf

### 4. 修改 configure.ac
已完成以下修改：
1. 添加了 libwebsockets 检查代码：
```shell
PKG_CHECK_MODULES([WEBSOCKETS], [libwebsockets >= 0.0.1],[
  AM_CONDITIONAL([HAVE_WEBSOCKETS],[true])],[
  AC_MSG_RESULT([no]); AM_CONDITIONAL([HAVE_WEBSOCKETS],[false])])
```

2. 添加了 mod_whisper 的 Makefile 到构建系统：
```shell
src/mod/asr_tts/mod_whisper/Makefile
```

### 5. 更新 modules.conf
需要在 modules.conf 中添加：
```
asr_tts/mod_whisper
```

### 6. 重新生成配置文件
需要运行以下命令：
```bash
autoreconf -fi
```

### 7. 重新编译 FreeSWITCH
需要运行：
```bash
./configure
make
make install
```

### 8. 配置 mod_whisper
完成编译后，需要配置 mod_whisper 以连接到 Whisper 服务器。

### 9. 集成 Ollama
最后一步是配置 Ollama 服务，使用 deepseek 模型来处理对话。

### 10. 安装结果

已完成以下步骤：
1. 安装了 libwebsockets 依赖
2. 修改了 configure.ac，添加了 mod_whisper 支持
3. 重新生成了配置文件（autoreconf -fi）
4. 重新编译并安装了 FreeSWITCH

### 11. 下一步配置

现在需要：
1. 配置 mod_whisper 连接到 Whisper 服务器
2. 设置 Ollama 服务
3. 配置 FreeSWITCH 拨号计划

要完成这些配置，我们需要：

1. 创建 mod_whisper 的配置文件：
```bash
vi /usr/local/freeswitch/conf/autoload_configs/whisper.conf.xml
```

2. 在 modules.conf.xml 中启用 mod_whisper：
```bash
vi /usr/local/freeswitch/conf/autoload_configs/modules.conf.xml
```
添加：
```xml
<load module="mod_whisper"/>
```

3. 配置拨号计划以使用 whisper：
```bash
vi /usr/local/freeswitch/conf/dialplan/default.xml
```

接下来我们将继续完成这些配置。

## 当前配置详情

### 1. mod_whisper 配置文件
位置：`/usr/local/freeswitch/conf/autoload_configs/whisper.conf.xml`

```xml
<configuration name="whisper.conf" description="Whisper ASR-TTS Configuration">
  <settings>
    <param name="asr-server-url" value="ws://192.168.11.2:8088/asr"/>
    <param name="tts-server-url" value="ws://192.168.11.2:8089/tts"/>
    <param name="return-json" value="1"/>
  </settings>
</configuration>
```

### 2. 关键参数说明
- ASR服务器：ws://192.168.11.2:8088/asr
- TTS服务器：ws://192.168.11.2:8089/tts
- 返回格式：JSON (return-json=1)

### 3. 拨号计划配置示例
```xml
<extension name="whisper_test">
  <condition field="destination_number" expression="^(1000)$">
    <action application="answer"/>
    <action application="sleep" data="1000"/>
    <action application="set" data="whisper_ws_timeout=10000"/>
    <action application="set" data="whisper_ws_retry=3"/>
    <action application="play_and_detect_speech" 
            data="silence_stream://1000 detect:whisper {no-input-timeout=5000,speech-timeout=2000,lang=zh}"/>
  </condition>
</extension>
```

### 4. 重要配置参数
1. WebSocket 超时设置：
   - whisper_ws_timeout=10000 (10秒)
   - whisper_ws_retry=3 (重试3次)

2. 语音识别参数：
   - no-input-timeout=5000 (无输入超时5秒)
   - speech-timeout=2000 (语音超时2秒)
   - lang=zh (中文识别)

### 5. 注意事项
1. ASR和TTS服务使用不同端口（8088/8089）以避免冲突
2. WebSocket需要支持二进制数据帧模式
3. 建议在局域网环境部署以确保稳定性
4. 配置了适当的超时时间和重试机制

### 6. 调试方法
1. 在fs_cli中开启调试日志：
```
console loglevel debug
```

2. 监控日志：
```bash
tail -f /usr/local/freeswitch/log/freeswitch.log | grep whisper
```

### 7. 性能建议
1. 系统要求：
   - 内存：建议4GB以上
   - CPU：建议4核心以上
   - 网络：稳定的局域网环境

2. 并发建议：
   - 单服务器建议最大并发数：50路
   - 需要根据实际硬件配置调整

### 8. 安全建议
1. 建议使用防火墙限制WebSocket访问
2. 考虑使用WSS（WebSocket Secure）
3. 定期检查日志文件
4. 监控系统资源使用情况

### 9. 源码修改和重新编译流程

#### 9.1 修改 mod_whisper.h
文件位置：`/usr/src/freeswitch/src/mod/asr_tts/mod_whisper/mod_whisper.h`

1. 添加TTS结构体字段：
```c
typedef struct {
    switch_memory_pool_t *pool;
    char *voice;
    char *engine;
    char *tts_server_url;  // 添加此字段
    struct lws *wsi;
    switch_buffer_t *audio_buffer;
    switch_thread_t *thread;
    int started;
    char *text;
    char *channel_uuid;
} whisper_tts_t;
```

#### 9.2 修改 mod_whisper.c
文件位置：`/usr/src/freeswitch/src/mod/asr_tts/mod_whisper/mod_whisper.c`

1. 在 `whisper_speech_text_param_tts` 函数中添加参数处理：
```c
void whisper_speech_text_param_tts(switch_speech_handle_t *sh, char *param, const char *val)
{
    whisper_tts_t *context = (whisper_tts_t *) sh->private_info;

    if (!zstr(param) && !zstr(val)) {
        if (!strcasecmp(param, "engine")) {
            context->engine = switch_core_strdup(sh->memory_pool, val);
        } else if (!strcasecmp(param, "voice")) {
            context->voice = switch_core_strdup(sh->memory_pool, val);
        } else if (!strcasecmp(param, "tts-server-url")) {
            context->tts_server_url = switch_core_strdup(sh->memory_pool, val);
        }
    }
}
```

2. 修改 `mod_whisper_load` 函数，注册TTS格式：
```c
SWITCH_MODULE_LOAD_FUNCTION(mod_whisper_load)
{
    // ... 其他代码 ...

    if ((status = switch_event_bind(modname, SWITCH_EVENT_RELOADXML, NULL, event_handler, NULL)) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't bind to reloadxml event!
");
        return SWITCH_STATUS_TERM;
    }

    // 注册TTS格式
    *module_interface = switch_loadable_module_create_module_interface(pool, modname);
    whisper_speech_interface = switch_loadable_module_create_interface(*module_interface, SWITCH_SPEECH_INTERFACE);
    whisper_speech_interface->interface_name = "whisper";
    whisper_speech_interface->speech_open = whisper_speech_open;
    whisper_speech_interface->speech_close = whisper_speech_close;
    whisper_speech_interface->speech_feed_tts = whisper_speech_feed_tts;
    whisper_speech_interface->speech_read_tts = whisper_speech_read_tts;
    whisper_speech_interface->speech_flush_tts = whisper_speech_flush_tts;
    whisper_speech_interface->speech_text_param_tts = whisper_speech_text_param_tts;
    whisper_speech_interface->speech_numeric_param_tts = whisper_speech_numeric_param_tts;
    whisper_speech_interface->speech_float_param_tts = whisper_speech_float_param_tts;

    return SWITCH_STATUS_SUCCESS;
}
```

#### 9.3 重新编译流程

1. 进入FreeSWITCH源码目录：
```bash
cd /usr/src/freeswitch
```

2. 清理之前的编译：
```bash
make clean mod_whisper-clean
```

3. 编译mod_whisper模块：
```bash
make mod_whisper-install
```

4. 重启FreeSWITCH：
```bash
systemctl restart freeswitch
```

5. 验证模块加载：
```bash
fs_cli -x 'module_exists mod_whisper'
```

#### 9.4 调试TTS功能

1. 在fs_cli中开启调试日志：
```
console loglevel debug
```

2. 测试TTS功能：
```
originate user/1000 &speak(whisper|default|Hello World)
```

3. 检查日志输出：
```bash
tail -f /usr/local/freeswitch/log/freeswitch.log | grep whisper
```

### 10. 常见问题解决
1. WebSocket连接问题：
   - 检查网络连接
   - 验证服务器地址和端口
   - 确认防火墙设置

2. 识别质量问题：
   - 调整语音超时参数
   - 检查音频质量
   - 验证语言设置

3. 性能问题：
   - 监控系统资源
   - 适当调整并发数
   - 优化网络环境

这些配置已经在当前环境中测试通过，可以作为新服务器部署的参考。

## mod_whisper 运行流程

### 1. 语音识别（ASR）流程

1. 呼叫建立
   ```
   2025-02-10 06:33:20.694632 [NOTICE] Channel [sofia/internal/1000@192.168.11.180] has been answered
   2025-02-10 06:33:20.694632 [DEBUG] Callstate Change EARLY -> ACTIVE
   ```

2. 初始化设置
   ```
   EXECUTE set(whisper_ws_timeout=10000)
   EXECUTE set(whisper_ws_retry=3)
   ```

3. WebSocket连接建立
   ```
   2025-02-10 06:33:21.714613 [DEBUG] WebSockets ASR client established. [0x7f1f0cf786f0]
   ```

4. 语音检测配置
   ```
   2025-02-10 06:33:21.734671 [DEBUG] mod_whisper.c:414 no-input-timeout = 5000
   2025-02-10 06:33:21.734671 [DEBUG] mod_whisper.c:417 speech-timeout = 2000
   ```

5. 语音数据处理
   ```
   2025-02-10 06:33:21.734671 [DEBUG] switch_core_io.c:448 Setting BUG Codec PCMA:8
   ```

6. 语音识别结果返回
   ```
   2025-02-10 06:33:21.734671 [NOTICE] mod_whisper.c:353 Final Result: {"status": "ok", "message": "配置已接收"}
   ```

### 2. 文本转语音（TTS）流程

1. TTS WebSocket连接建立
   ```
   2025-02-10 06:33:22.794733 [DEBUG] WebSockets TTS client established. [0x7f1f0cfae140]
   ```

2. TTS引擎初始化
   ```
   2025-02-10 06:33:22.814667 [DEBUG] switch_ivr_play_say.c:3108 OPEN TTS whisper
   ```

3. 音频编码设置
   ```
   2025-02-10 06:33:22.814667 [DEBUG] switch_ivr_play_say.c:3118 Raw Codec Activated
   ```

4. 文本处理和转换
   ```
   2025-02-10 06:33:22.854664 [DEBUG] switch_ivr_play_say.c:2826 Speaking text: 配置已接收
   ```

### 3. 会话结束流程

1. 正常挂机处理
   ```
   2025-02-10 06:33:23.874613 [NOTICE] Hangup sofia/internal/1000@192.168.11.180 [CS_EXECUTE] [NORMAL_CLEARING]
   ```

2. ASR资源释放
   ```
   2025-02-10 06:33:23.874613 [DEBUG] mod_whisper.c:166 ASR close!
   2025-02-10 06:33:23.874613 [WARNING] websock_glue.c:202 Websocket ASR client connection closed. 1
   ```

3. 会话状态清理
   ```
   2025-02-10 06:33:23.874613 [DEBUG] switch_core_state_machine.c:844 Callstate Change ACTIVE -> HANGUP
   ```

### 4. 重要注意点

1. 数据流处理
   - ASR和TTS使用独立的WebSocket连接
   - 音频数据使用PCMA编码
   - 支持二进制数据帧传输

2. 异常处理
   - 设置了WebSocket超时和重试机制
   - 实现了正常的资源释放
   - 支持会话状态的完整转换

3. 性能优化
   - 使用独立的线程处理WebSocket通信
   - 实现了异步的音频数据处理
   - 支持多路并发处理
