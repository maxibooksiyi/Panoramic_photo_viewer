#include <opencv2/opencv.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <locale>
#include <stdexcept>
#include <string>

#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ===================== 跨平台文件选择 =====================
#ifdef _WIN32

std::string openFileDialog() {
    OPENFILENAMEA ofn;
    char szFile[1024] = {0};

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter =
        "Image Files\0*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff\0"
        "All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = "Select Panorama Image";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        return std::string(szFile);
    }
    return "";
}

#else

std::string trimNewline(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

std::string runCommand(const std::string& cmd) {
    std::string tmpFile = "/tmp/panorama_path_" + std::to_string(getpid()) + ".txt";
    std::string fullCmd = cmd + " > \"" + tmpFile + "\" 2>/dev/null";

    int ret = system(fullCmd.c_str());
    if (ret != 0) {
        std::remove(tmpFile.c_str());
        return "";
    }

    FILE* fp = std::fopen(tmpFile.c_str(), "r");
    if (!fp) {
        std::remove(tmpFile.c_str());
        return "";
    }

    char buffer[8192] = {0};
    if (!std::fgets(buffer, sizeof(buffer), fp)) {
        std::fclose(fp);
        std::remove(tmpFile.c_str());
        return "";
    }

    std::fclose(fp);
    std::remove(tmpFile.c_str());

    return trimNewline(std::string(buffer));
}

std::string openFileDialog() {
    std::string result;

    result = runCommand("zenity --file-selection --title='选择全景图片'");
    if (!result.empty()) return result;

    result = runCommand("kdialog --getopenfilename \"$HOME\"");
    if (!result.empty()) return result;

    std::cout << "\n未能打开图形文件选择器，请手动输入图片路径：" << std::endl;
    std::cout << "路径> ";
    std::getline(std::cin, result);

    result = trimNewline(result);

    if (result.size() >= 2 && result.front() == '\'' && result.back() == '\'') {
        result = result.substr(1, result.size() - 2);
    }
    if (result.size() >= 2 && result.front() == '"' && result.back() == '"') {
        result = result.substr(1, result.size() - 2);
    }

    return result;
}

#endif

// ===================== 全景浏览器 =====================
class PanoramaViewer {
private:
    cv::Mat panorama;
    cv::Mat viewImage;
    cv::Mat canvas;
    cv::Mat mapX;
    cv::Mat mapY;

    float yaw;
    float pitch;
    float fov;

    bool isDraggingLeft;
    bool isDraggingRight;
    cv::Point lastMousePos;

    int imageWidth;
    int imageHeight;
    int panelHeight;
    int canvasWidth;
    int canvasHeight;

    bool showInfoPanel;
    bool needRemap;
    bool needCompose;

public:
    PanoramaViewer(const std::string& imagePath,
                   int winWidth = 1280,
                   int winHeight = 720,
                   int infoPanelHeight = 110)
        : yaw(0.0f),
          pitch(0.0f),
          fov(90.0f),
          isDraggingLeft(false),
          isDraggingRight(false),
          imageWidth(winWidth),
          imageHeight(winHeight),
          panelHeight(infoPanelHeight),
          canvasWidth(winWidth),
          canvasHeight(winHeight + infoPanelHeight),
          showInfoPanel(false),
          needRemap(true),
          needCompose(true) {
        std::setlocale(LC_ALL, "");

        panorama = cv::imread(imagePath, cv::IMREAD_COLOR);
        if (panorama.empty()) {
            throw std::runtime_error("无法加载图片: " + imagePath);
        }

        viewImage = cv::Mat(imageHeight, imageWidth, CV_8UC3);
        canvas = cv::Mat(canvasHeight, canvasWidth, CV_8UC3);
        mapX = cv::Mat(imageHeight, imageWidth, CV_32FC1);
        mapY = cv::Mat(imageHeight, imageWidth, CV_32FC1);
    }

    void clampView() {
        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;

        if (fov < 20.0f) fov = 20.0f;
        if (fov > 120.0f) fov = 120.0f;

        while (yaw > 180.0f) yaw -= 360.0f;
        while (yaw < -180.0f) yaw += 360.0f;
    }

    void markViewChanged() {
        needRemap = true;
        needCompose = true;
    }

    void markUiChanged() {
        needCompose = true;
    }

    void zoomByFactor(float factor) {
        fov *= factor;
        clampView();
        markViewChanged();
    }

    void zoomIn() {
        zoomByFactor(0.90f);
    }

    void zoomOut() {
        zoomByFactor(1.10f);
    }

    cv::Point2f sphericalToUV(float theta, float phi) const {
        float u = (theta + static_cast<float>(M_PI)) /
                  (2.0f * static_cast<float>(M_PI));
        float v = (phi + static_cast<float>(M_PI) / 2.0f) /
                  static_cast<float>(M_PI);

        u = std::fmod(u, 1.0f);
        if (u < 0.0f) u += 1.0f;

        v = 1.0f - v;
        v = std::max(0.0f, std::min(1.0f, v));

        float px = u * panorama.cols;
        float py = v * panorama.rows;

        if (px >= panorama.cols) px -= panorama.cols;
        if (px < 0.0f) px += panorama.cols;

        if (py < 0.0f) py = 0.0f;
        float maxY = static_cast<float>(panorama.rows - 1);
        if (py > maxY) py = maxY;

        return cv::Point2f(px, py);
    }

    void buildRemapMaps() {
        float fovRad = fov * static_cast<float>(M_PI) / 180.0f;
        float aspect = static_cast<float>(imageWidth) / static_cast<float>(imageHeight);
        float tanHalfFov = std::tan(fovRad / 2.0f);

        float yawRad = yaw * static_cast<float>(M_PI) / 180.0f;
        float pitchRad = pitch * static_cast<float>(M_PI) / 180.0f;

        float cosYaw = std::cos(yawRad);
        float sinYaw = std::sin(yawRad);
        float cosPitch = std::cos(pitchRad);
        float sinPitch = std::sin(pitchRad);

        for (int y = 0; y < imageHeight; ++y) {
            float* rowX = mapX.ptr<float>(y);
            float* rowY = mapY.ptr<float>(y);

            for (int x = 0; x < imageWidth; ++x) {
                float nx = (2.0f * x / static_cast<float>(imageWidth) - 1.0f) * aspect;
                float ny = 1.0f - 2.0f * y / static_cast<float>(imageHeight);

                float rayX = nx * tanHalfFov;
                float rayY = ny * tanHalfFov;
                float rayZ = -1.0f;

                float len = std::sqrt(rayX * rayX + rayY * rayY + rayZ * rayZ);
                rayX /= len;
                rayY /= len;
                rayZ /= len;

                float tempY = rayY * cosPitch - rayZ * sinPitch;
                float tempZ = rayY * sinPitch + rayZ * cosPitch;
                rayY = tempY;
                rayZ = tempZ;

                float tempX = rayX * cosYaw + rayZ * sinYaw;
                tempZ = -rayX * sinYaw + rayZ * cosYaw;
                rayX = tempX;
                rayZ = tempZ;

                float theta = std::atan2(rayX, -rayZ);
                float phi = std::asin(std::max(-1.0f, std::min(1.0f, rayY)));

                cv::Point2f uv = sphericalToUV(theta, phi);
                rowX[x] = uv.x;
                rowY[x] = uv.y;
            }
        }
    }

    void renderPanoramaToView() {
        buildRemapMaps();

        cv::remap(
            panorama,
            viewImage,
            mapX,
            mapY,
            cv::INTER_LINEAR,
            cv::BORDER_WRAP
        );

        needRemap = false;
    }

    void drawInfoPanel() {
        canvas.setTo(cv::Scalar(32, 32, 32));

        viewImage.copyTo(canvas(cv::Rect(0, 0, imageWidth, imageHeight)));

        cv::Rect panelRect(0, imageHeight, canvasWidth, panelHeight);
        cv::Mat panel = canvas(panelRect);
        panel.setTo(cv::Scalar(20, 20, 20));

        cv::line(canvas,
                 cv::Point(0, imageHeight),
                 cv::Point(canvasWidth - 1, imageHeight),
                 cv::Scalar(90, 90, 90), 1);

        std::string line1 =
            "Yaw: " + std::to_string(static_cast<int>(yaw)) +
            "    Pitch: " + std::to_string(static_cast<int>(pitch)) +
            "    FOV: " + std::to_string(static_cast<int>(fov));

        std::string line2 =
            "Mouse Left Drag: Rotate    Mouse Wheel: Zoom    Mouse Right Drag: Zoom";

        std::string line3 =
            "Keyboard: +/- or W/S Zoom, Arrow Keys Rotate, H Toggle Info, ESC Exit";

        cv::putText(panel, line1, cv::Point(20, 32),
                    cv::FONT_HERSHEY_SIMPLEX, 0.75, cv::Scalar(0, 255, 0), 2);

        cv::putText(panel, line2, cv::Point(20, 64),
                    cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(230, 230, 230), 1);

        cv::putText(panel, line3, cv::Point(20, 92),
                    cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(230, 230, 230), 1);

        needCompose = false;
    }

    void composeIfNeeded() {
        if (needRemap) {
            renderPanoramaToView();
            needCompose = true;
        }

        if (showInfoPanel && needCompose) {
            drawInfoPanel();
        } else if (!showInfoPanel) {
            needCompose = false;
        }
    }

    void updateWindowSize(const std::string& windowName) {
        if (showInfoPanel) {
            cv::resizeWindow(windowName, canvasWidth, canvasHeight);
        } else {
            cv::resizeWindow(windowName, imageWidth, imageHeight);
        }
    }

    void onMouse(int event, int x, int y, int flags) {
        bool inImageArea = (x >= 0 && x < imageWidth && y >= 0 && y < imageHeight);

        if (event == cv::EVENT_LBUTTONDOWN && inImageArea) {
            isDraggingLeft = true;
            lastMousePos = cv::Point(x, y);
        }
        else if (event == cv::EVENT_LBUTTONUP) {
            isDraggingLeft = false;
        }
        else if (event == cv::EVENT_RBUTTONDOWN && inImageArea) {
            isDraggingRight = true;
            lastMousePos = cv::Point(x, y);
        }
        else if (event == cv::EVENT_RBUTTONUP) {
            isDraggingRight = false;
        }
        else if (event == cv::EVENT_MOUSEMOVE) {
            if (isDraggingLeft) {
                int dx = x - lastMousePos.x;
                int dy = y - lastMousePos.y;

                yaw += dx * 0.2f;
                pitch += dy * 0.2f;
                clampView();

                lastMousePos = cv::Point(x, y);
                markViewChanged();
            }
            else if (isDraggingRight) {
                int dy = y - lastMousePos.y;

                if (dy < 0) {
                    zoomIn();
                } else if (dy > 0) {
                    zoomOut();
                }

                lastMousePos = cv::Point(x, y);
            }
        }
        else if (event == cv::EVENT_MOUSEWHEEL) {
            int delta = cv::getMouseWheelDelta(flags);
            if (delta > 0) {
                zoomIn();
            } else if (delta < 0) {
                zoomOut();
            }
        }
    }

    static void mouseCallback(int event, int x, int y, int flags, void* userdata) {
        PanoramaViewer* self = reinterpret_cast<PanoramaViewer*>(userdata);
        if (self) {
            self->onMouse(event, x, y, flags);
        }
    }

    void toggleInfoPanel(const std::string& windowName) {
        showInfoPanel = !showInfoPanel;
        markUiChanged();
        updateWindowSize(windowName);
    }

    void handleKeyboard(int key, const std::string& windowName) {
        if (key == 81 || key == 2424832) {
            yaw -= 3.0f;
            clampView();
            markViewChanged();
            return;
        }
        if (key == 83 || key == 2555904) {
            yaw += 3.0f;
            clampView();
            markViewChanged();
            return;
        }
        if (key == 82 || key == 2490368) {
            pitch -= 3.0f;
            clampView();
            markViewChanged();
            return;
        }
        if (key == 84 || key == 2621440) {
            pitch += 3.0f;
            clampView();
            markViewChanged();
            return;
        }

        if (key == '+' || key == '=' || key == 'w' || key == 'W') {
            zoomIn();
            return;
        }

        if (key == '-' || key == '_' || key == 's' || key == 'S') {
            zoomOut();
            return;
        }

        if (key == 'h' || key == 'H') {
            toggleInfoPanel(windowName);
            return;
        }
    }

    void show() {
        const std::string windowName = "Panorama Viewer";

        cv::namedWindow(windowName, cv::WINDOW_NORMAL);
        updateWindowSize(windowName);
        cv::setMouseCallback(windowName, mouseCallback, this);

        std::cout << "操作说明：" << std::endl;
        std::cout << "  鼠标左键拖动：旋转视角" << std::endl;
        std::cout << "  鼠标滚轮：缩放视野" << std::endl;
        std::cout << "  鼠标右键上下拖动：缩放视野" << std::endl;
        std::cout << "  键盘 + / - 或 W / S：缩放视野" << std::endl;
        std::cout << "  方向键：旋转视角" << std::endl;
        std::cout << "  H：显示/隐藏信息栏" << std::endl;
        std::cout << "  ESC：退出" << std::endl;

        composeIfNeeded();
        if (showInfoPanel) {
            cv::imshow(windowName, canvas);
        } else {
            cv::imshow(windowName, viewImage);
        }

        cv::waitKeyEx(30);

        while (true) {
            composeIfNeeded();

            if (showInfoPanel) {
                cv::imshow(windowName, canvas);
            } else {
                cv::imshow(windowName, viewImage);
            }

            int key = cv::waitKeyEx(10);

            if (key == 27) {
                break;
            }
            if (key != -1) {
                handleKeyboard(key, windowName);
            }
        }

        cv::destroyAllWindows();
    }
};

// ===================== 主函数 =====================
int main() {
    std::setlocale(LC_ALL, "");

    std::cout << "请选择全景图片..." << std::endl;

    std::string imagePath = openFileDialog();
    if (imagePath.empty()) {
        std::cerr << "未选择任何图片，程序退出。" << std::endl;
        return -1;
    }

    std::cout << "已选择: [" << imagePath << "]" << std::endl;

    try {
        PanoramaViewer viewer(imagePath);
        viewer.show();
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
