#include <signal.h>
#include <memory.h>
#include <array>
#include "socketholder.h"

using namespace std;

socketholder::socketholder() : isStop(false), pools(5)
{
    for (int i = 0; i < READ_LOOP_MAX; i++)
    {
        rwatchers[i] = obtain_event_base();
        watcher_thread[i] = std::thread([this, i]() {
            event_base_loop(rwatchers[i].get(), EVLOOP_NO_EXIT_ON_EMPTY);
            cout << "in loop id: " << i << endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        });
    }
}
socketholder::~socketholder()
{
}

void socketholder::onConnect(evutil_socket_t fd)
{
    if (isStop)
    {
        cout << " socketholder is stop" << endl;
        return;
    }
    std::unique_lock<std::mutex> lock(syncMutex);
    auto id = fd % READ_LOOP_MAX;
    cout << "fd is: " << fd << endl;
    std::shared_ptr<channel> pChan = std::make_shared<channel>(shared_from_this(), fd);
    pChan->startWatcher();
    chns[id].emplace(fd, pChan->shared_from_this());
}

void socketholder::onDisconnect(evutil_socket_t fd)
{
    std::unique_lock<std::mutex> lock(syncMutex);

    auto id = fd % READ_LOOP_MAX;
    chns[id].erase(fd);
    cout << "socketholder onDisconnect" << endl;
    if (isStop && chns[id].size() == 0)
    {
        event_base_loopexit(rwatchers[id].get(), nullptr);
        cout << "onDisconnect exit loop id: " << id << endl;
    }
}
void socketholder::closeIdleChannel()
{
    std::unique_lock<std::mutex> lock(syncMutex);
    for (int i = 0; i < READ_LOOP_MAX; i++)
    {
        if (chns[i].size() == 0)
        {
            event_base_loopexit(rwatchers[i].get(), nullptr);
            cout << "closeIdleChannel exit loop id: " << i << endl;
        }
        else
        {
            for (auto &kv : chns[i])
            {
                kv.second->closeSafty();
            }
        }
    }
}
void socketholder::waitStop()
{

    cout << " socketholer waitStop" << endl;
    isStop = true;
    closeIdleChannel();
    for (int i = 0; i < READ_LOOP_MAX; i++)
    {
        if (watcher_thread[i].joinable())
        {
            watcher_thread[i].join();
        }
    }
}

std::shared_ptr<channel> socketholder::getChannel(evutil_socket_t fd)
{
    std::unique_lock<std::mutex> lock(syncMutex);
    auto pair = chns[fd % READ_LOOP_MAX].find(fd);
    if (pair->second != nullptr)
    {
        return pair->second->shared_from_this();
    }
    else
    {
        return nullptr;
    }
}