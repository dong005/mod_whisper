# mod_whisper

一个基于Whisper的FreeSWITCH模块，用于语音识别。

## 功能特点

- 支持语音转文字
- 支持WebSocket连接
- 支持多种音频格式
- 支持VAD语音检测

## 配置说明

配置文件位于 `conf/autoload_configs/whisper.conf`：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<configuration name="whisper.conf" description="Whisper ASR配置">
  <settings>
    <param name="asr-server-url" value="ws://192.168.11.2:8080/asr"/>
    <param name="tts-server-url" value="ws://192.168.11.2:8080/tts"/>
    <param name="return-json" value="1"/>
  </settings>
</configuration>
```

## 安装说明

1. 克隆此仓库到FreeSWITCH源码目录：
```bash
cd ${FREESWITCH_SRC}/src/mod/asr_tts/
git clone https://github.com/dong005/mod_whisper.git
```

2. 在 `configure.ac` 中添加模块和WebSocket支持：
```
# 添加模块
src/mod/asr_tts/mod_whisper/Makefile

# 添加WebSocket支持
PKG_CHECK_MODULES([WEBSOCKETS], [libwebsockets >= 0.0.1],[
  AM_CONDITIONAL([HAVE_WEBSOCKETS],[true])],[
  AC_MSG_RESULT([no]); AM_CONDITIONAL([HAVE_WEBSOCKETS],[false])])
```

3. 在 `modules.conf` 中添加：
```
asr_tts/mod_whisper
```

4. 编译安装：
```bash
autoreconf -f
make
make install
```

5. 在 `modules.conf.xml` 中启用模块：
```xml
<load module="mod_whisper"/>
```

6. 在拨号计划中配置ASR：
```xml
<!-- 在 conf/dialplan/default.xml 中添加 -->
<extension name="asr_test">
  <condition field="destination_number" expression="^(8888)$">
    <action application="lua" data="asr_tts.lua"/>
  </condition>
</extension>
```

7. 创建Lua脚本 `scripts/asr_tts.lua`：
```lua
session:answer()
session:set_tts_params("tts_commandline", "zh-CN-YunxiNeural")

-- ASR配置
session:execute("set", "asr_engine=whisper")
session:execute("set", "asr_language=zh-CN")
session:execute("detect_speech", "whisper default")

-- 等待识别结果
local max_wait = 50  -- 最多等待5秒
local wait_count = 0
while wait_count < max_wait do
    session:sleep(100)  -- 每次等待100ms
    local result = session:getVariable("detect_speech_result")
    if result then
        freeswitch.consoleLog("notice", "ASR结果: " .. result .. "\n")
        session:speak(result)
        break
    end
    wait_count = wait_count + 1
end

-- 停止识别
session:execute("detect_speech", "stop")

-- 如果没有结果
if not session:getVariable("detect_speech_result") then
    freeswitch.consoleLog("warning", "未获取到ASR结果\n")
    session:speak("抱歉，没有听清您说的话")
end

session:hangup()
```

8. 配置TTS命令行 `conf/autoload_configs/tts_commandline.conf`：
```xml
<configuration name="tts_commandline.conf" description="TextToSpeech Commandline configuration">
    <settings>
        <param name="command" value="sh $${base_dir}/scripts/tts.sh ${voice} ${file} ${text}"/>
        <param name="auto-unlink" value="false"/>
    </settings>
    <ext-maps>
        <map ext="mp3" voice="zh-CN-YunxiNeural"/>
        <map ext="mp3" voice="zh-CN-YunyangNeural"/>
        <map ext="mp3" voice="zh-CN-XiaoxiaoNeural"/>
        <map ext="mp3" voice="zh-CN-XiaoyiNeural"/>
        <map ext="mp3" voice="zh-CN-YunjianNeural"/>
        <map ext="mp3" voice="zh-CN-YunxiaNeural"/>
    </ext-maps>
</configuration>
```

9. 创建TTS脚本 `scripts/tts.sh`：
```bash
#!/bin/bash
VOICE=$1
FILE=$2
TEXT=$3
edge-tts --voice $VOICE --text "$TEXT" --write-media $FILE
```

注意：
1. 需要安装edge-tts：`pip install edge-tts`
2. 确保tts.sh有执行权限：`chmod +x tts.sh`
3. 确保已加载mod_tts_commandline模块

## 待开发功能

- [ ] 基础模块框架
- [ ] Whisper集成
- [ ] 音频处理
- [ ] API接口设计

## 许可证

MPL 1.1

## How to setup

1. Clone this repository in `${FREESWITCH_SOURCE_ROOT}/src/mod/asr_tts/` directory 

2. Add `src/mod/asr_tts/mod_whisper/Makefile` in `configure.ac` under `AC_CONFIG_FILES` section

2.1. Add following snippet in `configure.ac` 

```
PKG_CHECK_MODULES([WEBSOCKETS], [libwebsockets >= 0.0.1],[
  AM_CONDITIONAL([HAVE_WEBSOCKETS],[true])],[
  AC_MSG_RESULT([no]); AM_CONDITIONAL([HAVE_WEBSOCKETS],[false])])
```
3. Add the following two module in the ${FREESWITCH_SOURCE_ROOT}/modules.conf
```
asr_tts/mod_whisper
```

4. Run `autoreconf -f` before compiling freeswitch

5. Re-compile and install the freeswitch to install mod_whisper module.


6. Active the `mod_whisper` by add the following to lines into the `${FREESWITCH_INSTALLATION_ROOT}/conf/autoload_configs/modules.conf.xml`
<load module="mod_whisper"/>

7. Copy the lua script under `{FREESWITCH_INSTALLATION_ROOT}/scripts/`

8. Bind a number to build application by adding the following xml settings to the `${FREESWITCH_INSTALLATION_ROOT}/conf/dialplan/default.xml`
#   m o d _ w h i s p e r 
 
 