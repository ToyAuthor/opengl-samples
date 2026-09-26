# ------------------------------------------------------------------------------
# 這邊列的都是 OpenGL 4.6 的範例
# 建議從 sample01 開始看，這是所有範例裡面最簡單的一個
# 而且註解寫最多，sample01 寫過的註解，在其它範例裡就不會再解釋一遍了
# ------------------------------------------------------------------------------

# 描繪最基本的三角形
add_subdirectory(sample01)

# 用非同步方式將 vertex 資料、矩陣傳送至顯卡
# 使用 FPS 遊戲方式操作視角
add_subdirectory(sample02)

# 用非同步方式將 vertex 資料、矩陣傳送至顯卡
# 使用 FPS 遊戲方式操作視角
# 示範 PBO(Pixel Buffer Object) 非同步上傳 texture 的做法
add_subdirectory(sample03)

# 用非同步方式將矩陣、glMultiDrawElementsIndirect 所需資料傳送至顯卡
# 沒有使用 PBO 非同步上傳圖片
# 使用 FPS 遊戲方式操作視角
# 使用 texture array 來儲存圖片
# 搭配 SSBO 來儲存 bindless texture 的 handle
add_subdirectory(sample04)
