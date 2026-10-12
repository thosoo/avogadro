/**********************************************************************
  BuilderPool implementation (see builderpool.h).
 *****************************************************************/

#include "builderpool.h"

#include <QMetaType>

namespace Avogadro {
namespace rendering {

namespace {
//! One-time metatype registration so TessellationResult can cross threads in a
//! queued signal (ChunkStore<T> is implicitly shared, so the copy is cheap).
struct TessellationResultMetatype {
  TessellationResultMetatype()
  {
    qRegisterMetaType<TessellationResult>();
  }
};
} // namespace

BuilderWorker::BuilderWorker(BuilderPool *pool, QObject *parent)
  : QThread(parent), m_pool(pool)
{
}

void BuilderWorker::run()
{
  // Spin until the pool stops. Each iteration waits for a queued job,
  // tessellates it (pure, GL-free), and hands the result back via a queued
  // signal to the pool's GUI thread.
  while (true) {
    BuilderPool::Job job;
    {
      QMutexLocker locker(&m_pool->m_mutex);
      while (!m_pool->m_stopping && m_pool->m_queue.isEmpty())
        m_pool->m_workAvailable.wait(&m_pool->m_mutex);
      if (m_pool->m_stopping)
        return;  // stopping
      job = m_pool->m_queue.takeFirst();
    }

    TessellationResult result = snapshotTessellateBSDY(job.snapshot, job.params);
    emit jobReady(0, result, job.generation);
    {
      QMutexLocker locker(&m_pool->m_mutex);
      m_pool->m_pending -= 1;
    }
  }
}

BuilderPool::BuilderPool(int numThreads, QObject *parent)
  : QObject(parent), m_pending(0), m_numThreads(0), m_stopping(false)
{
  static TessellationResultMetatype s_metatype;
  if (numThreads < 1)
    numThreads = 1;
  m_numThreads = numThreads;
  startWorkers();
}

BuilderPool::~BuilderPool()
{
  stopWorkers();
}

void BuilderPool::startWorkers()
{
  for (int i = 0; i < m_workers.size(); ++i)
    delete m_workers[i];
  m_workers.clear();
  m_pending = 0;
  for (int i = 0; i < m_numThreads; ++i) {
    BuilderWorker *w = new BuilderWorker(this, this);
    m_workers.append(w);
    // The worker emits jobReady(); connect with a queued connection so the
    // result is delivered on this (GUI) thread's event loop.
    connect(w, &BuilderWorker::jobReady, this, &BuilderPool::jobReady,
            Qt::QueuedConnection);
  }
  for (int i = 0; i < m_workers.size(); ++i)
    m_workers[i]->start();
}

void BuilderPool::stopWorkers()
{
  {
    QMutexLocker locker(&m_mutex);
    m_stopping = true;
    m_workAvailable.wakeAll();
  }
  for (int i = 0; i < m_workers.size(); ++i) {
    BuilderWorker *w = m_workers[i];
    if (w->isRunning())
      w->wait();
    delete w;
  }
  m_workers.clear();
  m_pending = 0;
}

void BuilderPool::submit(const SceneSnapshot &snapshot, const JobParams &params,
                         unsigned int generation)
{
  Job job;
  job.snapshot = snapshot;
  job.params = params;
  job.generation = generation;
  {
    QMutexLocker locker(&m_mutex);
    m_queue.append(job);
    m_pending += 1;
    m_workAvailable.wakeOne();
  }
}

int BuilderPool::pendingCount() const
{
  QMutexLocker locker(&m_mutex);
  return m_pending;
}

} // namespace rendering
} // namespace Avogadro

#include "builderpool.moc"
