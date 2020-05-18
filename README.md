## HttpServer
用C++实现的一个Http服务器
- epoll的ET模式
- 使用reactor事件处理模式 + 线程池
- 日志系统（单例模式 + 异步）
- 可以解析HTTP的GET或POST请求报文，可以请求的内容包括视频、图片、pdf文件等；
- webbench测试结果如下：

![](./resource/webbench.PNG)

