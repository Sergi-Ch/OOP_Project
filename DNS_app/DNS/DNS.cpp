#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#include <windows.h>
#include <string>
#include <thread>
#include <map>
#include <vector>
#include <mutex>
#include <chrono>

// Подключаем Boost.Asio
#include <boost/asio.hpp>

// Глобальные переменные для элементов управления
HWND g_hEditDomain = nullptr;
HWND g_hEditIP = nullptr;
HWND g_hCacheInfo = nullptr; // Для отображения информации о кэше

// Структура для кэша DNS-записей
struct DNSCacheEntry {
    std::vector<std::string> ips;
    std::chrono::steady_clock::time_point timestamp;
};

// Класс для кэширования DNS-запросов
class DNSCache {
private:
    std::map<std::string, DNSCacheEntry> cache;
    mutable std::mutex cache_mutex;
    std::chrono::seconds ttl; // Время жизни кэша (5 минут)

public:
    DNSCache(std::chrono::seconds ttl_seconds = std::chrono::seconds(300))
        : ttl(ttl_seconds) {}

    // Получить IP-адреса из кэша
    bool get(const std::string& domain, std::vector<std::string>& result) {
        std::lock_guard<std::mutex> lock(cache_mutex);

        auto it = cache.find(domain);
        if (it != cache.end()) {
            auto now = std::chrono::steady_clock::now();
            auto age = now - it->second.timestamp;

            if (age < ttl) {
                // Запись еще актуальна
                result = it->second.ips;
                return true;
            }
            else {
                // Запись устарела - удаляем
                cache.erase(it);
            }
        }
        return false;
    }

    // Добавить запись в кэш
    void put(const std::string& domain, const std::vector<std::string>& ips) {
        std::lock_guard<std::mutex> lock(cache_mutex);

        DNSCacheEntry entry;
        entry.ips = ips;
        entry.timestamp = std::chrono::steady_clock::now();
        cache[domain] = entry;
    }

    // Очистить кэш
    void clear() {
        std::lock_guard<std::mutex> lock(cache_mutex);
        cache.clear();
    }

    // Получить статистику кэша
    size_t size() const {
        std::lock_guard<std::mutex> lock(cache_mutex);
        return cache.size();
    }

    // Удалить устаревшие записи
    void cleanup() {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto now = std::chrono::steady_clock::now();

        for (auto it = cache.begin(); it != cache.end(); ) {
            auto age = now - it->second.timestamp;
            if (age >= ttl) {
                it = cache.erase(it);
            }
            else {
                ++it;
            }
        }
    }
};

// Глобальный кэш
DNSCache g_dnsCache;

// Объявляем структуру для передачи результата через PostMessage
struct ResultMsg
{
    std::string ip;
    bool fromCache;
};

// Объявляем пользовательские сообщения
const UINT WM_DNS_RESULT = WM_USER + 1;
const UINT WM_UPDATE_TITLE = WM_USER + 2;

// Функция для получения IP-адресов с использованием кэша
std::vector<std::string> resolveDNSWithCache(const std::string& domain, bool& fromCache) {
    std::vector<std::string> result;

    // Сначала пробуем получить из кэша
    if (g_dnsCache.get(domain, result)) {
        fromCache = true;
        return result;
    }

    fromCache = false;

    // Если нет в кэше, выполняем реальный DNS-запрос
    try {
        boost::asio::io_context io;
        boost::asio::ip::tcp::resolver resolver(io);

        // Используем синхронный запрос для простоты
        auto endpoints = resolver.resolve(domain, "http");

        for (const auto& endpoint : endpoints) {
            result.push_back(endpoint.endpoint().address().to_string());
        }

        // Сохраняем результат в кэш (если что-то нашли)
        if (!result.empty()) {
            g_dnsCache.put(domain, result);
        }
    }
    catch (const boost::system::system_error& e) {
        result.push_back("Ошибка: " + std::string(e.what()));
    }
    catch (const std::exception& e) {
        result.push_back("Ошибка: " + std::string(e.what()));
    }

    return result;
}

// Функция обработки сообщений окна
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        // Создаём поле для ввода домена
        g_hEditDomain = CreateWindowEx(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT,
            20, 50, 300, 25,
            hwnd, (HMENU)101, GetModuleHandle(nullptr), nullptr);

        // Кнопка "Найти IP"
        CreateWindowEx(
            0, L"BUTTON", L"Найти IP",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            330, 50, 80, 25,
            hwnd, (HMENU)102, GetModuleHandle(nullptr), nullptr);

        // Кнопка "Очистить"
        CreateWindowEx(
            0, L"BUTTON", L"Очистить",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            330, 90, 80, 25,
            hwnd, (HMENU)104, GetModuleHandle(nullptr), nullptr);

        // Кнопка "Очистить кэш"
        CreateWindowEx(
            0, L"BUTTON", L"Очистить кэш",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            330, 130, 80, 25,
            hwnd, (HMENU)105, GetModuleHandle(nullptr), nullptr);

        // Поле для вывода IP (только для чтения)
        g_hEditIP = CreateWindowEx(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_READONLY,
            20, 90, 300, 25,
            hwnd, (HMENU)103, GetModuleHandle(nullptr), nullptr);

        // Поле для информации о кэше
        g_hCacheInfo = CreateWindowEx(
            0, L"STATIC", L"Кэш: 0 записей",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 130, 200, 25,
            hwnd, nullptr, GetModuleHandle(nullptr), nullptr);

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
                    bool fromCache = false;
                    auto ips = resolveDNSWithCache(domain, fromCache);

                    // Формируем строку для вывода
                    std::string resultText;
                    if (fromCache) {
                        resultText += "[Из кэша] ";
                    }

                    if (ips.empty()) {
                        resultText = "Адрес не найден";
                    }
                    else if (ips.size() == 1) {
                        resultText += ips[0];
                    }
                    else {
                        for (size_t i = 0; i < ips.size(); ++i) {
                            resultText += ips[i];
                            if (i < ips.size() - 1) {
                                resultText += ", ";
                            }
                        }
                    }

                    // Передаём результат в основной поток через PostMessage
                    ResultMsg* msgData = new ResultMsg{ resultText, fromCache };
                    PostMessage(hwnd, WM_DNS_RESULT, 0, reinterpret_cast<LPARAM>(msgData));
                }
                catch (const std::exception& e) {
                    std::string errorMsg = "Ошибка: " + std::string(e.what());
                    ResultMsg* msgData = new ResultMsg{ errorMsg, false };
                    PostMessage(hwnd, WM_DNS_RESULT, 0, reinterpret_cast<LPARAM>(msgData));
                }
            }).detach();
        }
        else if (LOWORD(wParam) == 104) // Кнопка "Очистить"
        {
            SetWindowText(g_hEditDomain, L"");
            SetWindowText(g_hEditIP, L"");
        }
        else if (LOWORD(wParam) == 105) // Кнопка "Очистить кэш"
        {
            g_dnsCache.clear();
            // Обновляем информацию о кэше
            wchar_t cacheInfo[50];
            wsprintf(cacheInfo, L"Кэш: %d записей", g_dnsCache.size());
            SetWindowText(g_hCacheInfo, cacheInfo);
            MessageBox(hwnd, L"Кэш очищен", L"Информация", MB_ICONINFORMATION);
        }
        break;
    }

    case WM_DNS_RESULT:
    {
        ResultMsg* msgData = reinterpret_cast<ResultMsg*>(lParam);
        
        // Устанавливаем текст в поле вывода
        SetWindowTextA(g_hEditIP, msgData->ip.c_str());
        
        // Обновляем информацию о кэше
        wchar_t cacheInfo[50];
        wsprintf(cacheInfo, L"Кэш: %d записей", g_dnsCache.size());
        SetWindowText(g_hCacheInfo, cacheInfo);
        
        // Показываем всплывающую подсказку, если использован кэш
        if (msgData->fromCache) {
            SetWindowText(hwnd, L"DNS Resolver [использован кэш]");
            
            // Через 2 секунды вернем обычный заголовок
            std::thread([hwnd]() {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                PostMessage(hwnd, WM_UPDATE_TITLE, 0, 0);
            }).detach();
        }
        
        delete msgData; // освобождаем память
        break;
    }
    
    case WM_UPDATE_TITLE:
    {
        // Возвращаем обычный заголовок окна
        SetWindowText(hwnd, L"DNS Resolver");
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

// Таймер для периодической очистки кэша
VOID CALLBACK CacheCleanupTimer(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    g_dnsCache.cleanup();

    // Обновляем информацию о кэше
    if (g_hCacheInfo) {
        wchar_t cacheInfo[50];
        wsprintf(cacheInfo, L"Кэш: %d записей", g_dnsCache.size());
        SetWindowText(g_hCacheInfo, cacheInfo);
    }
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
        0, CLASS_NAME, L"DNS Resolver с кэшем",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 200,
        nullptr, nullptr, hInstance, nullptr);

    if (hwnd == nullptr)
    {
        MessageBox(nullptr, L"Не удалось создать окно!", L"Ошибка", MB_ICONERROR);
        return 1;
    }

    // Запускаем таймер для очистки кэша каждые 60 секунд
    SetTimer(hwnd, 1, 60000, CacheCleanupTimer);

    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Удаляем таймер
    KillTimer(hwnd, 1);

    return 0;
}
