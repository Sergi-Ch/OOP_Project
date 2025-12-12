#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#include <windows.h>
#include <string>
#include <thread>

// Подключаем Boost.Asio
#include <boost/asio.hpp>

// Глобальные переменные для элементов управления
HWND g_hEditDomain = nullptr;
HWND g_hEditIP = nullptr;

// Объявляем структуру для передачи результата через PostMessage
struct ResultMsg
{
    std::string ip;
};

// Объявляем пользовательское сообщение
const UINT WM_DNS_RESULT = WM_USER + 1;

// Функция обработки сообщений окна
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        // Поле для ввода домена
        g_hEditDomain = CreateWindowEx(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT,
            20, 50, 300, 25,
            hwnd, (HMENU)101, GetModuleHandle(nullptr), nullptr);

        // Кнопка "Найти IP"
        HWND hButton = CreateWindowEx(
            0, L"BUTTON", L"Найти IP",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            330, 50, 80, 25,
            hwnd, (HMENU)102, GetModuleHandle(nullptr), nullptr);

        // Кнопка "Очистить"
        HWND hClearBtn = CreateWindowEx(
            0, L"BUTTON", L"Очистить",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            330, 90, 80, 25,
            hwnd, (HMENU)104, GetModuleHandle(nullptr), nullptr);

        // Поле для вывода IP (только для чтения)
        g_hEditIP = CreateWindowEx(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_READONLY,
            20, 90, 300, 25,
            hwnd, (HMENU)103, GetModuleHandle(nullptr), nullptr);

        // Метки
        CreateWindowEx(0, L"STATIC", L"Домен:", WS_CHILD | WS_VISIBLE,
            20, 30, 50, 15, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        CreateWindowEx(0, L"STATIC", L"IP-адрес:", WS_CHILD | WS_VISIBLE,
            20, 75, 60, 15, hwnd, nullptr, GetModuleHandle(nullptr), nullptr);

        break;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == 102) // Кнопка "Найти IP"
        {
            wchar_t buffer[256];
            GetWindowText(g_hEditDomain, buffer, 256);
            std::wstring wdomain(buffer);
            std::string domain(wdomain.begin(), wdomain.end());

            if (domain.empty())
            {
                MessageBox(hwnd, L"Введите доменное имя!", L"Ошибка", MB_ICONWARNING);
                return 0;
            }

            // Запускаем DNS-запрос в отдельном потоке
            std::thread([domain, hwnd]() {
                try {
                    boost::asio::io_context io;
                    boost::asio::ip::tcp::resolver resolver(io);

                    // Используем новый API: передаём хост и сервис напрямую
                    resolver.async_resolve(
                        domain, "http",
                        [hwnd](const boost::system::error_code& ec,
                            boost::asio::ip::tcp::resolver::results_type results) {
                                std::string ip;
                                if (!ec && !results.empty()) {
                                    ip = results.begin()->endpoint().address().to_string();
                                }
                                else {
                                    ip = "Адрес не найден";
                                }

                                // Передаём результат в основной поток через PostMessage
                                ResultMsg* msgData = new ResultMsg{ ip };
                                PostMessage(hwnd, WM_DNS_RESULT, 0, reinterpret_cast<LPARAM>(msgData));
                        }
                    );

                    io.run(); // Запускаем обработку асинхронной операции
                }
                catch (const std::exception& e) {
                    std::string errorMsg = "Ошибка: " + std::string(e.what());
                    ResultMsg* msgData = new ResultMsg{ errorMsg };
                    PostMessage(hwnd, WM_DNS_RESULT, 0, reinterpret_cast<LPARAM>(msgData));
                }
                }).detach();
        }
        else if (LOWORD(wParam) == 104) // Кнопка "Очистить"
        {
            SetWindowText(g_hEditDomain, L"");
            SetWindowText(g_hEditIP, L"");
        }
        break;
    }

    case WM_DNS_RESULT:
    {
        ResultMsg* msgData = reinterpret_cast<ResultMsg*>(lParam);
        SetWindowTextA(g_hEditIP, msgData->ip.c_str());
        delete msgData; // освобождаем память
        break;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// Точка входа WinMain
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    const wchar_t CLASS_NAME[] = L"DNSResolverClass";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClass(&wc))
    {
        MessageBox(nullptr, L"Не удалось зарегистрировать класс окна!", L"Ошибка", MB_ICONERROR);
        return 1;
    }

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, L"DNS Resolver",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 200,
        nullptr, nullptr, hInstance, nullptr);

    if (hwnd == nullptr)
    {
        MessageBox(nullptr, L"Не удалось создать окно!", L"Ошибка", MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}