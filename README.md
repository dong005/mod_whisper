# mod_whisper

一个FreeSWITCH模块，通过WebSocket接口连接语音识别和TTS服务器。

## 功能特点

- 支持语音转文字
- 支持WebSocket连接
- 支持多种音频格式
- 支持VAD语音检测

## 安装步骤

1. 克隆仓库
   ```bash
   cd ${FREESWITCH_SOURCE_ROOT}/src/mod/asr_tts/
   git clone [repository_url] mod_whisper
   ```

2. 配置构建环境
   
   在`configure.ac`的`AC_CONFIG_FILES`部分添加以下内容：
   ```
   src/mod/asr_tts/mod_whisper/Makefile
   ```

3. 修改模块配置
   
   在`${FREESWITCH_SOURCE_ROOT}/modules.conf`中添加以下模块：
   ```
   asr_tts/mod_whisper
   ```

4. 重新构建
   ```bash
   autoreconf -f
   make
   make install
   ```

5. 激活模块
   
   在`${FREESWITCH_INSTALLATION_ROOT}/conf/autoload_configs/modules.conf.xml`中添加以下行：
   ```xml
   <load module="mod_whisper"/>
   ```

6. 复制脚本
   
   将Lua脚本复制到FreeSWITCH脚本目录：
   ```bash
   cp scripts/*.lua ${FREESWITCH_INSTALLATION_ROOT}/scripts/
   ```

7. 配置拨号计划
   
   在`${FREESWITCH_INSTALLATION_ROOT}/conf/dialplan/default.xml`中添加相应的拨号计划配置。

## 配置说明

在`${FREESWITCH_INSTALLATION_ROOT}/conf/autoload_configs/whisper.conf`中配置服务器连接信息：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<configuration name="whisper.conf" description="Whisper ASR-TTS Configuration">
  <settings>
    <param name="asr-server-url" value="ws://your_server:port/asr"/>
    <param name="tts-server-url" value="ws://your_server:port/tts"/>
    <param name="return-json" value="1"/>
  </settings>
</configuration>
```

## 依赖项

- FreeSWITCH
- libwebsockets >= 0.0.1

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