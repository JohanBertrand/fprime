// ======================================================================
// \title  SocketReadTask.cpp
// \author mstarch
// \brief  cpp file for SocketReadTask implementation class
//
// \copyright
// Copyright 2009-2020, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================

#include <Drv/Ip/SocketReadTask.hpp>
#include <Fw/Logger/Logger.hpp>
#include <Fw/Types/Assert.hpp>
#include <cerrno>
#include <string.h>

#define MAXIMUM_SIZE 0x7FFFFFFF

namespace Drv {

SocketReadTask::SocketReadTask() : m_reconnect(false), m_stop(false) {}

SocketReadTask::~SocketReadTask() {}

void SocketReadTask::startSocketTask(const Fw::StringBase &name,
                                     const bool reconnect,
                                     const bool reuse_address,
                                     const Os::Task::ParamType priority,
                                     const Os::Task::ParamType stack,
                                     const Os::Task::ParamType cpuAffinity) {
    FW_ASSERT(not m_task.isStarted());  // It is a coding error to start this task multiple times
    FW_ASSERT(not this->m_stop);        // It is a coding error to stop the thread before it is started
    m_reconnect = reconnect;
    m_reuse_address = reuse_address;
    // Note: the first step is for the IP socket to open the port
    Os::Task::TaskStatus stat = m_task.start(name, SocketReadTask::readTask, this, priority, stack, cpuAffinity);
    FW_ASSERT(Os::Task::TASK_OK == stat, static_cast<NATIVE_INT_TYPE>(stat));
}

SocketIpStatus SocketReadTask::startup(const bool reuse_address) {
    return this->getSocketHandler().startup(reuse_address);
}

SocketIpStatus SocketReadTask::open(const bool reuse_address) {
    SocketIpStatus status = this->getSocketHandler().open(reuse_address);
    // Call connected any time the open is successful
    if (Drv::SOCK_SUCCESS == status) {
        this->connected();
    }
    return status;
}

void SocketReadTask::shutdown() {
    Fw::Logger::logMsg("SocketReadTask::shutdown\n");
    this->getSocketHandler().shutdown();
}

void SocketReadTask::close() {
    Fw::Logger::logMsg("SocketReadTask::close\n");
    this->getSocketHandler().close();
}

Os::Task::TaskStatus SocketReadTask::joinSocketTask(void** value_ptr) {
    return m_task.join(value_ptr);
}

void SocketReadTask::stopSocketTask() {
    this->m_stop = true;
    this->getSocketHandler().shutdown();  // Break out of any receives and fully shutdown
}

void SocketReadTask::readTask(void* pointer) {
    FW_ASSERT(pointer);
    SocketIpStatus status = SOCK_SUCCESS;
    SocketReadTask* self = reinterpret_cast<SocketReadTask*>(pointer);
    do {
        self->m_task_lock.lock();
        printf("readTask <------------1\n");
        // Open a network connection if it has not already been open
        if ((not self->getSocketHandler().isStarted()) and (not self->m_stop) and
            ((status = self->startup(self->m_reuse_address)) != SOCK_SUCCESS)) {
            Fw::Logger::logMsg(
                "[WARNING] Failed to open port with status %d and errno %d\n",
                static_cast<POINTER_CAST>(status),
                static_cast<POINTER_CAST>(errno));
            (void) Os::Task::delay(SOCKET_RETRY_INTERVAL_MS);
            printf("readTask unlock isStarted\n");
            self->m_task_lock.unlock();
            continue;
        }
        printf("readTask <------------2\n");

        // Open a network connection if it has not already been open
        if ((not self->getSocketHandler().isOpened()) and (not self->m_stop) and
            ((status = self->open(self->m_reuse_address)) != SOCK_SUCCESS)) {
            Fw::Logger::logMsg(
                "[WARNING] Failed to open port with status %d and errno %d\n",
                static_cast<POINTER_CAST>(status),
                static_cast<POINTER_CAST>(errno));
            (void) Os::Task::delay(SOCKET_RETRY_INTERVAL_MS);
            printf("readTask unlock isOpened\n");
            self->m_task_lock.unlock();
            continue;
        }
        printf("readTask <------------3\n");

        // If the network connection is open, read from it
        if (self->getSocketHandler().isStarted() and self->getSocketHandler().isOpened() and (not self->m_stop)) {
            Fw::Buffer buffer = self->getBuffer();
            U8* data = buffer.getData();
            FW_ASSERT(data);
            U32 size = buffer.getSize();
            printf("buffer size: %u\n",size);
            status = self->getSocketHandler().recv(data, size);
            printf("self->getSocketHandler().recv(data, size);");
            if ((status != SOCK_SUCCESS) && (status != SOCK_INTERRUPTED_TRY_AGAIN)) {
                Fw::Logger::logMsg("[WARNING] Failed to recv from port with status %d and errno %d and stop %d\n",
                static_cast<POINTER_CAST>(status),
                static_cast<POINTER_CAST>(errno),
                static_cast<POINTER_CAST>(self->m_stop));
                Fw::Logger::logMsg(strerror(errno));
                Fw::Logger::logMsg("\n");
                printf("self->getSocketHandler().close()\n");
                self->getSocketHandler().close();
                buffer.setSize(0);
            } else {
                // Send out received data
                buffer.setSize(size);
            }
            self->sendBuffer(buffer, status);
        }
        self->m_task_lock.unlock();
        printf("readTask unlock last\n");

        /*if (self->m_need_to_close)
        {
            Fw::Logger::logMsg("closed\n");
            self->m_need_to_close = false;
            self->getSocketHandler().close();
            self->m_stop = true;
        }*/
    }
    // As long as not told to stop, and we are successful interrupted or ordered to retry, keep receiving
    while (not self->m_stop &&
           (status == SOCK_SUCCESS || status == SOCK_INTERRUPTED_TRY_AGAIN || self->m_reconnect));
    self->getSocketHandler().shutdown(); // Shutdown the port entirely
}
}  // namespace Drv
