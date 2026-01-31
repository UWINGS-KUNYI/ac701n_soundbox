# AC701N_Soundbox_SDK

AC701N 系列通用蓝牙SDK 固件程序

## 快速开始

欢迎使用杰理开源项目，在开始进入项目之前，请详细阅读SDK 介绍文档， 从而获得对杰理系列芯片和SDK 的大概认识，并且可以通过快速开始介绍来进行开发.

## 样品链接

[AC701N样品购买链接](https://item.taobao.com/item.htm?id=1016552311375)

## 工具链

关于如何获取杰理工具链 和 如何进行环境搭建，请阅读以下内容：

编译工具 ：请安装杰理编译工具来搭建起编译环境, [下载链接](https://doc.yunthinker.com/#/docs/Environment/%E7%A8%8B%E5%BA%8F%E5%BC%80%E5%8F%91%E7%9B%B8%E5%85%B3%E5%B7%A5%E5%85%B7/1) 

USB 升级工具 : 在开发完成后，需要使用杰理烧写工具将对应的烧录文件烧写到目标板，进行开发调试, 关于如何获取工具请进入申请[链接](https://item.taobao.com/item.htm?id=1004292405691)并详细阅读对应的[文档](https://doc.yunthinker.com/#/docs/Environment/%E7%A8%8B%E5%BA%8F%E5%BC%80%E5%8F%91%E7%9B%B8%E5%85%B3%E5%B7%A5%E5%85%B7/6)

## 介绍文档

SDK 版本信息 : [SDK 历史版本信息](https://gitcode.com/yunthinker/AC701N_Soundbox_SDK/blob/main/doc/JL701N_soundbox_SDK%E5%8F%91%E5%B8%83%E7%89%88%E6%9C%AC%E4%BF%A1%E6%81%AF.pdf)

SDK 介绍文档 : [SDK快速开始简介](https://gitcode.com/yunthinker/AC701N_Soundbox_SDK/blob/main/doc/JL701N_soundbox_SDK_%E4%BB%8B%E7%BB%8D.pdf)

## 编译工程

在安装好工具链后，即可开始进行编译。

默认情况下，SDK 支持在 Windows 系统下进行编译以及下载。在 Linux 系统上，仅支持编译。

SDK 发布的时候，默认发布了 .cbp 后缀的 Code::Blocks 工程。此外，还同时发布了 Makefile 以及 VSCode 的配置。用户可以自行选择使用命令行进行编译。

- [使用 Code::Blocks 进行编译](https://doc.yunthinker.com/#/docs/Environment/%E7%A8%8B%E5%BA%8F%E5%BC%80%E5%8F%91%E7%9B%B8%E5%85%B3%E5%B7%A5%E5%85%B7/2?id=_21-%e4%bd%bf%e7%94%a8-codeblocks-%e8%bf%9b%e8%a1%8c%e7%bc%96%e8%af%91)
- [使用 make 进行编译](https://doc.yunthinker.com/#/docs/Environment/%E7%A8%8B%E5%BA%8F%E5%BC%80%E5%8F%91%E7%9B%B8%E5%85%B3%E5%B7%A5%E5%85%B7/2?id=_22-%e4%bd%bf%e7%94%a8-make-%e8%bf%9b%e8%a1%8c%e7%bc%96%e8%af%91)
- [使用 VSCode 进行编译](https://doc.yunthinker.com/#/docs/Environment/%E7%A8%8B%E5%BA%8F%E5%BC%80%E5%8F%91%E7%9B%B8%E5%85%B3%E5%B7%A5%E5%85%B7/2?id=_23-%e4%bd%bf%e7%94%a8-vscode-%e8%bf%9b%e8%a1%8c%e7%bc%96%e8%af%91)
