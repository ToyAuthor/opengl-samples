# sample05
Geometry 與 SSAO pass 共用同一個 UniformBlock

## 描繪的物體
一個共用的立方體網格(含 normal/tangent/uv)，
以不同 model 矩陣擺出地板與五個柱體方塊，
方塊夾角處正是 SSAO 最容易觀察的地方。

## 展示功能

### Camera
使用 FPS 遊戲方式操作視角

### Normal Mapping
BumpHeight() 以程式產生磚塊 + 圓凸的高度場，
再用中央差分轉成切線空間 Normal Map；
VS 把 TBN 轉到 view space 並做 Gram-Schmidt 正交化，
避免非等向縮放(地板 12×0.4×12)造成切線歪斜。

### SSAO
延遲著色的 G-Buffer 採 GL_RGBA16F 儲存 view space 位置與法線；
32 個半球取樣點以二次曲線往中心集中，
搭配 4×4 噪點做隨機旋轉，最後用 4×4 box blur 去除條紋。

### 打光
Lighting pass 用 Blinn-Phong(view space 下攝影機位於原點，故 viewDir = -fragPos)，
AO 只乘在環境光項上，並做 gamma 校正。
