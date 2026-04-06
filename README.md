# 全景照片浏览工具
ubuntu平台使用，基于opencv  

双线性插值  
支持 cv::remap 渲染  
信息栏默认隐藏，按 H 显示/隐藏  
左键拖动旋转  
鼠标滚轮缩放  
右键上下拖动缩放  
键盘 +/- 或 W/S 缩放  
方向键旋转  
ESC 退出  


编译命令 
```
g++ -o panorama_viewer panorama_viewer.cpp `pkg-config --cflags --libs opencv` -std=c++11
```
如果是opencv4
```
g++ -o panorama_viewer panorama_viewer.cpp $(pkg-config --cflags --libs opencv4) -std=c++11
```

运行命令 
```
./panorama_viewer
```
