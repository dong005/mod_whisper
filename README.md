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

2. 在 `configure.ac` 中添加模块：
```
src/mod/asr_tts/mod_whisper/Makefile
```

3. 添加WebSocket支持：
```
PKG_CHECK_MODULES([WEBSOCKETS], [libwebsockets >= 0.0.1],[
  AM_CONDITIONAL([HAVE_WEBSOCKETS],[true])],[
  AC_MSG_RESULT([no]); AM_CONDITIONAL([HAVE_WEBSOCKETS],[false])])
```

4. 在 `modules.conf` 中添加：
```
asr_tts/mod_whisper
```

5. 编译安装：
```bash
autoreconf -f
make
make install
```

6. 在 `modules.conf.xml` 中启用模块：
```xml
<load module="mod_whisper"/>
```

## 许可证

MPL 1.1

## 待开发功能

- [ ] 基础模块框架
- [ ] Whisper集成
- [ ] 音频处理
- [ ] API接口设计

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