# 這邊列的都是 OpenGL 4.6 的範例
# sample01 身為第一個範例，上面會有最多的註解

# 描繪最基本的三角形
add_subdirectory(sample01)

# 用非同步方式將頂點資料、矩陣傳送至顯卡
# 使用 FPS 遊戲方式操作視角
add_subdirectory(sample02)

# 用非同步方式將矩陣、材質貼圖傳送至顯卡
# 頂點資料改為靜態用法
# 使用 FPS 遊戲方式操作視角
# 使用 texture array 來儲存圖片
add_subdirectory(sample03)
