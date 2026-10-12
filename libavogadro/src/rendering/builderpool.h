/**********************************************************************
  BuilderPool - worker threads that build tessellated chunk stores.

  The ported Engine classes are GUI-affine, so they cannot run on a worker.
  Instead the pure SnapshotTessellator (rendering/snapshottessellator.h) does
  the sphere/bond/line emission off the GUI thread. The pool owns a small
  fixed set of worker threads (2-3, design A7) that consume (snapshot,
  JobParams, generation) jobs and hand the resulting ChunkStores back to the
  GUI thread via a queued signal, which uploads the ready chunks inside
  paintGL under the <=2 ms/frame budget (D2/D3).

  Job results are tagged with the revision (generation) the GUI thread built
  them for. A later submission bumps the generation; when a stale result
  arrives the GUI thread discards it instead of uploading geometry for a
  molecule that has already changed.

  This replaces the per-frame re-emission of renderModern() (task 6.1).
 *****************************************************************/

#ifndef AVOGADRO_RENDERING_BUILDERPOOL_H
#define AVOGADRO_RENDERING_BUILDERPOOL_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QVector>

#include "snapshottessellator.h"

namespace Avogadro {
namespace rendering {

class BuilderPool;  // forward decl: BuilderWorker holds a BuilderPool*

//! A single worker thread. Loops: wait for a job, tessellate it, emit
//! jobReady() (queued to the pool's GUI thread), repeat until stop().
class BuilderWorker : public QThread
{
  Q_OBJECT

public:
  BuilderWorker(BuilderPool *pool, QObject *parent = nullptr);

signals:
  //! Emitted (as a queued connection) when a job's result is ready.
  void jobReady(unsigned long jobId, const TessellationResult &result,
                unsigned int generation);

protected:
  void run() override;

private:
  BuilderPool *m_pool;
};

//! Owns a fixed set of BuilderWorkers plus the mutex-guarded job queue they
//! share. Lives on the GUI thread. submit() enqueues a job and wakes a worker;
//! jobReady() fires on the GUI thread so it can upload ready chunks.
class BuilderPool : public QObject
{
  Q_OBJECT

public:
  explicit BuilderPool(int numThreads = 2, QObject *parent = nullptr);
  ~BuilderPool() override;

  //! Submit a build job. generation is the molecule revision the caller built
  //! the snapshot for; it is echoed back so the GUI thread can discard stale
  //! results. Jobs are queued and processed by the first idle worker.
  void submit(const SceneSnapshot &snapshot, const JobParams &params,
              unsigned int generation);

  //! Whether any submitted job has not yet produced a result.
  int pendingCount() const;

  //! Number of worker threads (for diagnostics/tests).
  int workerCount() const { return m_workers.size(); }

  //! Expose the worker objects so a caller can connect to each worker's own
  //! jobReady() (used by tests to isolate delivery from the pool's forward).
  QVector<BuilderWorker *> workers() const { return m_workers; }

signals:
  //! Emitted (queued) on the GUI thread when a job's result is ready. The
  //! generation lets the caller discard results for an earlier revision.
  void jobReady(unsigned long jobId, const TessellationResult &result,
                unsigned int generation);

private:
  struct Job {
    SceneSnapshot snapshot;
    JobParams params;
    unsigned int generation;
  };

  void startWorkers();
  void stopWorkers();

  friend class BuilderWorker;  // the worker loop reads the shared queue state

  mutable QMutex m_mutex;
  QWaitCondition m_workAvailable;
  QVector<Job> m_queue;
  int m_pending;
  int m_numThreads;
  bool m_stopping;
  QVector<BuilderWorker *> m_workers;
};

} // namespace rendering
} // namespace Avogadro

#endif // AVOGADRO_RENDERING_BUILDERPOOL_H
