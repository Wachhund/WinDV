/*
 * DShow.h — DirectShow pipeline class declarations for WinDV.
 *
 * This header declares the complete object model used to capture DV video
 * from a FireWire (IEEE 1394) device to AVI files and to play AVI files
 * back to a DV device.  All pipeline construction and frame routing is
 * implemented in DShow.cpp.
 *
 * Pipeline overview — Capture mode:
 *
 *   CDVInput  ──(frames)──>  CDV::HandleFrame
 *                               │
 *                               └──> CDVQueue  ──(CapturingThread)──>  CAVIWriter
 *                                                                   └──> CMonitor (preview)
 *
 * Pipeline overview — Record mode:
 *
 *   CAVIJoiner ──(frames)──>  CDV::HandleFrame
 *                                │
 *                                └──> CDVQueue  ──(RecordingThread)──>  CDVOutput
 *                                                                    └──> CMonitor (preview)
 *
 * Threading model:
 *   - CapturingThread / RecordingThread — consume frames from CDVQueue
 *   - MonitoringThread  — throttled preview delivery inside CMonitor
 *   - JoinerThread      — sequential file switching inside CAVIJoiner
 *
 * Error handling:
 *   HRESULT values from DirectShow calls are checked via CHECK_HR().
 *   On failure, CDShowException is thrown and caught by MFC TRY/CATCH_ALL.
 */

/* Posted to the parent window when the DV recording timestamp changes.
 * wParam is unused; lParam is the new time_t value (0 = no timestamp). */
#define WM_DV_TIMECHANGE	(WM_USER+201)

/* Posted to the parent window when free disk space drops below the threshold.
 * wParam is unused; lParam is the remaining free space in megabytes. */
#define WM_DV_LOWDISKSPACE	(WM_USER+202)

/* Posted to the parent window when no DV frames have been received for
 * m_autoStopTimeout milliseconds, indicating end of tape or device disconnect.
 * wParam and lParam are unused. */
#define WM_DV_SIGNALLOST	(WM_USER+203)

/*
 * CDShowException
 *
 * MFC exception class for all DirectShow errors.  Carries a human-readable
 * message string and a cause code so that callers can distinguish between
 * "device not found" and generic pipeline errors.
 *
 * Instances are heap-allocated with b_AutoDelete = TRUE so that MFC's
 * CATCH_ALL / DELETE_EXCEPTION macro handles cleanup automatically.
 *
 * Use ThrowDShowException() rather than constructing directly.
 */
class CDShowException: public CException {
public:
	CDShowException(BOOL b_AutoDelete, int cause, LPCSTR message);
	int m_cause;
	enum {none = 0, deviceNotFound, error};
	CString m_message;
	/* Fills lpszError with m_message; required by MFC CException protocol. */
	virtual BOOL GetErrorMessage(LPTSTR lpszError, UINT nMaxError, PUINT pnHelpContext = NULL);
protected:
};

/* Allocates and throws a CDShowException (always auto-delete). */
void ThrowDShowException(int cause, LPCSTR message);

/*
 * CFrameHandler
 *
 * Abstract sink interface.  Any object that can receive decoded DV frames
 * implements this interface.  The pipeline calls HandleFrame() for each
 * frame delivered by a DirectShow graph.
 *
 * A duration of -1 with data == NULL signals end-of-stream.
 */
class CFrameHandler {
public:
	/* Called for each DV frame.
	 * duration  - Frame duration in 100-nanosecond units (REFERENCE_TIME).
	 *             Pass -1 with data == NULL to signal end of stream.
	 * data      - Pointer to the raw DV frame bytes, or NULL for EOS.
	 * len       - Frame length in bytes (120000 for NTSC, 144000 for PAL). */
	virtual void HandleFrame(REFERENCE_TIME duration, BYTE *data, int len) = 0;
};

/*
 * CFrameSource
 *
 * Abstract source interface.  Objects that can produce DV frames implement
 * this interface, allowing CDV to treat CDVInput and CAVIJoiner uniformly.
 */
class CFrameSource {
public:
	/* Fills *type with the media type of the stream this source produces. */
	virtual void GetMediaType(CMediaType *type) = 0;
	/* Starts the graph running; delivered frames are forwarded to handler. */
	virtual void Run(CFrameHandler *handler) = 0;
	/* Stops the graph and clears the handler reference. */
	virtual void Stop() = 0;
};

/*
 * CFilterGraph
 *
 * Base class for all DirectShow graph wrappers in WinDV.  Constructs the
 * standard set of COM objects shared by every graph:
 *   m_GB  - ICaptureGraphBuilder2: high-level pin-connection helper
 *   m_FG  - IGraphBuilder:         low-level filter graph manager
 *   m_MC  - IMediaControl:         Run / Pause / Stop the graph
 *   m_MS  - IMediaSeeking:         duration and position queries
 *   m_ME  - IMediaEventEx:         completion events (used in AVI/DV output)
 *
 * Derived classes add filters to m_FG and wire them with m_GB.
 */
class CFilterGraph {
public:
	ICaptureGraphBuilder2 *m_GB;  /* high-level capture graph builder */
	IGraphBuilder	*m_FG;        /* filter graph manager */
	IMediaControl *m_MC;          /* graph run/pause/stop control */
	IMediaSeeking *m_MS;          /* seeking and duration queries */
	IMediaEventEx *m_ME;          /* graph completion event signaling */
	CFilterGraph();
	virtual ~CFilterGraph();
};

/*
 * CInputGraph
 *
 * A filter graph that terminates in a custom sink filter (CInputFilter /
 * CInputPin).  The sink pin intercepts every IMediaSample delivered by
 * the upstream graph and forwards it to m_handler as a raw frame via
 * CFrameHandler::HandleFrame().
 *
 * This design bridges DirectShow's push model into the application's own
 * frame-routing layer without requiring a separate registered filter CLSID.
 *
 * Subclasses (CAVIReader, CDVInput) are responsible for building the
 * upstream portion of the graph (file source + AVI splitter, or FireWire
 * capture filter) and connecting it to m_inputFilter.
 */
class CInputGraph:public CFilterGraph, CFrameSource {
public:
	/* Current frame destination; set by Run(), cleared by Stop(). */
	CFrameHandler *m_handler;

	/*
	 * CInputFilter
	 *
	 * Minimal CBaseFilter implementation that owns exactly one input pin.
	 * Added to the filter graph under the name "InputFilter" so that the
	 * graph builder can connect the upstream source to it automatically.
	 * Forwards all received samples and end-of-stream notifications to
	 * the parent CInputGraph's m_handler.
	 */
	class CInputFilter: public CBaseFilter {
	public:
		CInputGraph *m_graph;  /* back-pointer to owning CInputGraph */

		CInputFilter(CInputGraph *graph);
		~CInputFilter();
		int GetPinCount();
		CBasePin *GetPin(int n);
		CCritSec m_cs;  /* filter-level critical section required by CBaseFilter */

		/*
		 * CInputPin
		 *
		 * CBaseInputPin specialisation that accepts DV interleaved media
		 * (MEDIATYPE_Interleaved) and forwards each sample to the handler.
		 *
		 * CheckMediaType() accepts any MEDIATYPE_Interleaved stream, which
		 * covers both Type-1 (interleaved DV) and Type-2 (separate video +
		 * audio) after muxing.
		 *
		 * Receive() is the hot path: called once per frame (~25 or 30 Hz).
		 * It extracts the frame timestamp and raw bytes, then calls
		 * m_graph->m_handler->HandleFrame().
		 *
		 * EndOfStream() signals end-of-stream by calling HandleFrame with
		 * duration = -1, data = NULL.
		 */
		class CInputPin : public CBaseInputPin {
		public:
			CInputPin(CInputFilter *pFilter, CCritSec *cs, HRESULT *phr);
			/* Accepts MEDIATYPE_Interleaved; rejects all other types. */
			HRESULT CheckMediaType(const CMediaType *pmt);
			/* Hot path: extracts timestamp + bytes and forwards to handler. */
			STDMETHODIMP Receive(IMediaSample *pSample);
			/* Signals EOS to the handler (duration=-1, data=NULL, len=0). */
    		STDMETHODIMP EndOfStream();

		} *m_input;
	} *m_inputFilter;

	CInputGraph();
	/* Retrieves the negotiated media type from the connected pin.
	 * Ensures sample size is at least 144000 bytes (PAL worst-case). */
	void GetMediaType(CMediaType *type);
	/* Starts the filter graph; delivered frames are sent to handler. */
	void Run(CFrameHandler *handler);
	/* Stops the filter graph and clears the handler reference. */
	void Stop();
	~CInputGraph();
};

/*
 * COutputGraph
 *
 * A filter graph that originates from a custom source filter (COutputFilter /
 * COutputPin).  Application code calls HandleFrame() to inject raw DV frame
 * data, which is packaged into an IMediaSample and pushed downstream through
 * the DirectShow graph to whatever sink is connected (AVI mux, DV device,
 * or video renderer).
 *
 * m_time tracks the running presentation timestamp so that each sample
 * receives a monotonically increasing time range [m_time, m_time+duration].
 *
 * The optional m_queue parameter enables asynchronous delivery: when > 0,
 * COutputPin::Active() creates a COutputQueue that buffers samples on a
 * separate thread, preventing the calling thread from blocking on slow sinks.
 */
class COutputGraph:public CFilterGraph, public CFrameHandler {
public:
	/* Negotiated media type, set at construction time from the caller. */
	CMediaType m_type;
	/* Running presentation timestamp (100-ns units); advanced by each frame's duration. */
	REFERENCE_TIME m_time;

	/* Number of asynchronous output queue slots (0 = synchronous delivery). */
	int m_queue;

	/*
	 * COutputFilter
	 *
	 * Minimal CBaseFilter implementation that owns exactly one output pin.
	 * Added to the filter graph under the name "OutputFilter".  The graph
	 * builder connects it to the downstream sink filter (mux, renderer, etc.).
	 */
	class COutputFilter : public CBaseFilter {
	public:
		COutputGraph *m_graph;  /* back-pointer to owning COutputGraph */

		COutputFilter(COutputGraph *graph);
		~COutputFilter();
		int GetPinCount();
		CBasePin *GetPin(int n);
		CCritSec m_cs;  /* filter-level critical section required by CBaseFilter */

		/*
		 * COutputPin
		 *
		 * CBaseOutputPin specialisation that advertises the DV media type
		 * negotiated at graph construction time and delivers samples injected
		 * by COutputGraph::HandleFrame().
		 *
		 * When the graph's m_queue > 0, Active() creates a COutputQueue
		 * (DirectShow async delivery helper) so that HandleFrame() can return
		 * quickly without waiting for the downstream filter to consume each
		 * sample.  Inactive() tears it down.
		 *
		 * Deliver() and DeliverEndOfStream() route through COutputQueue when
		 * present, or fall back to synchronous CBaseOutputPin delivery.
		 */
		class COutputPin : public CBaseOutputPin {
		public:
			/* Async delivery queue; NULL when synchronous delivery is used. */
			COutputQueue *m_queue;
			COutputPin(COutputFilter *pFilter, CCritSec *cs, HRESULT *phr);
			/* Reports m_graph->m_type as the single supported media type. */
			HRESULT GetMediaType(int iPosition, CMediaType *pmt);
			/* Accepts only the exact type advertised by GetMediaType(). */
			HRESULT CheckMediaType(const CMediaType *pmt);
			/* Ensures allocator buffer is large enough for one DV frame. */
			HRESULT DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *ppropInputRequest);
			/* Routes sample through COutputQueue if present, else synchronous. */
			HRESULT Deliver(IMediaSample *pSample);
			/* Routes EOS through COutputQueue if present, else synchronous. */
			HRESULT DeliverEndOfStream();
			/* Creates COutputQueue when graph transitions to running state. */
			HRESULT Active();
			/* Destroys COutputQueue when graph transitions to stopped state. */
			HRESULT Inactive();
		} *m_output;
	} *m_outputFilter;

	/* type     - DV media type; copied into m_type for lifetime of the graph.
	 * queue    - Async queue depth (0 = synchronous, >0 = dedicated queue thread). */
	COutputGraph(CMediaType *type, int queue = 0);
	~COutputGraph();
	/* Packages data into an IMediaSample and pushes it into the graph.
	 * A NULL data pointer is ignored (no sample is delivered). */
	void HandleFrame(REFERENCE_TIME duration, BYTE *data, int len);
};

/*
 * CAVIReader
 *
 * CInputGraph subclass that reads DV frames from an AVI file.
 *
 * Graph topology:
 *   FileSource -> AVI Splitter -> [DV Mux (Type-2 only)] -> CInputFilter
 *
 * For Type-1 AVI files (interleaved DV stream), the AVI Splitter's
 * MEDIATYPE_Interleaved output pin connects directly to CInputFilter.
 * For Type-2 files (separate video + audio streams), a DV Mux filter is
 * inserted to re-combine them into an interleaved stream before delivery.
 */
class CAVIReader:public CInputGraph {
public:
	/* filename - Full path to the AVI source file (ANSI string). */
	CAVIReader(LPCSTR filename);
};

/*
 * CAVIJoiner
 *
 * CFrameSource that concatenates multiple AVI files into a single logical
 * stream, forwarding frames to a downstream CFrameHandler as if they came
 * from one continuous source.
 *
 * Operation:
 *   1. Construction: parses the pipe-delimited filenames string, expands
 *      wildcards, sorts each glob result, and opens the first file via
 *      CAVIReader.
 *   2. Run(): starts a background JoinerThread and starts the first reader.
 *   3. HandleFrame(): passes data frames upstream; on EOS (data==NULL)
 *      signals JoinerThread via m_ev.
 *   4. JoinerThread(): waits on m_ev, destroys the finished CAVIReader,
 *      opens the next file, and restarts.  When all files are exhausted it
 *      sends an EOS to the downstream handler and exits.
 *   5. Stop(): sets m_stopping, signals m_ev to unblock JoinerThread,
 *      waits for it to finish, then tears down the current reader.
 *
 * The filenames parameter uses '|' as a separator; each token may be a
 * glob pattern (e.g. "C:\capture\*.avi").
 */
class CAVIJoiner:public CFrameSource, CFrameHandler {
public:
	/* Downstream handler that receives the joined frame stream. */
	CFrameHandler *m_joinHandler;
	/* Sorted, expanded list of all AVI file paths to play in sequence. */
	CArray<CString, CString&> m_filenames;
	/* Currently open AVI reader (one at a time). */
	CAVIReader *m_reader;
	/* Signaled when the current reader reaches EOS, waking JoinerThread. */
	CEvent m_ev;
	/* Background thread that manages reader lifecycle. */
	CWinThread *m_thread;
	/* Index into m_filenames of the next file to open. */
	int m_current;
	/* Set to TRUE during Stop() so that HandleFrame() and JoinerThread() exit cleanly. */
	bool m_stopping;

	/* filenames - Pipe-separated list of paths / glob patterns. */
	CAVIJoiner(LPCSTR filenames);
	~CAVIJoiner();
	/* Delegates to the current CAVIReader. */
	void GetMediaType(CMediaType *type);
	/* Starts JoinerThread and starts the first CAVIReader. */
	void Run(CFrameHandler *handler);
	/* Signals JoinerThread to stop and waits for it to finish. */
	void Stop();
	/* Forwards live frames to m_joinHandler; signals m_ev on EOS. */
	void HandleFrame(REFERENCE_TIME duration, BYTE *data, int len);
	/* Thread body: advances through m_filenames until all files are played. */
	void JoinerThread();
};

/*
 * CDVControl
 *
 * Wraps IAMExtTransport to send transport-control commands (play, stop,
 * record, pause) to a DV camcorder or VTR over the FireWire bus.
 *
 * CtrlAttach() queries IAMExtTransport from the device filter's IUnknown.
 * If the device does not support IAMExtTransport the interface pointer
 * remains NULL and all Ctrl*() calls are silently no-ops.
 *
 * CtrlPause() first sends ED_MODE_PLAY then immediately ED_MODE_FREEZE
 * because some devices require this two-step sequence to enter pause.
 */
class CDVControl {
	/* IAMExtTransport interface on the connected DV device (may be NULL). */
	IAMExtTransport *m_ET;
public:
	CDVControl();
	~CDVControl();
	/* Queries IAMExtTransport from pDev; replaces any previously attached interface. */
	void CtrlAttach(IUnknown *pDev);
	/* Sends ED_MODE_STOP. */
	void CtrlStop();
	/* Sends ED_MODE_PLAY. */
	void CtrlPlay();
	/* Sends ED_MODE_PLAY then ED_MODE_FREEZE (two-step pause sequence). */
	void CtrlPause();
	/* Sends ED_MODE_RECORD. */
	void CtrlRecord();
	/* Sends ED_MODE_RECORD_FREEZE (record-pause / standby). */
	void CtrlRecPause();
};

/*
 * CDVInput
 *
 * CInputGraph + CDVControl combination that captures live DV frames from
 * a named FireWire capture device.
 *
 * Graph topology:
 *   DV capture filter (FireWire) -> CInputFilter
 *
 * m_DF (IAMDroppedFrames) is queried from the capture filter's output pin
 * so that the UI can report dropped frames during capture.
 *
 * CDVControl is initialised with CtrlAttach(pVSRC) so that the camcorder
 * can be commanded to play / pause / stop from the application.
 */
class CDVInput:public CInputGraph, public CDVControl {
public:
	/* Dropped-frame counter queried from the capture pin. */
	IAMDroppedFrames * m_DF;
	/* vsrc - Friendly name of the FireWire capture device (from EnumVideoDevices). */
	CDVInput(LPCSTR vsrc);
	~CDVInput();
	/* Returns the total number of frames dropped since the graph was started. */
	long GetDroppedFrames();
};

/*
 * CDVOutput
 *
 * COutputGraph + CDVControl combination that records DV frames to a named
 * FireWire output device (camcorder in record mode).
 *
 * Graph topology:
 *   COutputFilter -> DV output filter (FireWire device)
 *
 * An async output queue of depth 10 is used so that HandleFrame() on the
 * recording thread does not block waiting for the DV device to accept each
 * frame.
 *
 * The graph is started immediately in the constructor; the destructor waits
 * up to 5 seconds for the graph to complete (EC_COMPLETE) before stopping.
 */
class CDVOutput:public COutputGraph, public CDVControl {
public:
	/* vdst - Friendly name of the FireWire output device.
	 * type - DV media type (must match the frames that will be delivered). */
	CDVOutput(LPCSTR vdst, CMediaType *type);
	~CDVOutput();
};

/*
 * CAVIWriter
 *
 * COutputGraph subclass that writes DV frames to an AVI file.
 *
 * File naming:
 *   Files are initially written to a temporary name prefixed with "~"
 *   (m_tmpfile) to avoid leaving a partial AVI if capture is aborted.
 *   On destruction, after the graph completes, the temporary file is
 *   renamed to the final name (m_filename) via MoveFile().
 *
 *   The final filename is generated by GetCaptureFilename(), which appends
 *   an optional date/time suffix (dtformat / tim) and an auto-incrementing
 *   numeric suffix (ndigits) to avoid overwriting existing files.
 *
 * AVI type selection:
 *   type2AVI == true  -> Type-2 AVI: DV Splitter separates audio and video
 *                        into distinct AVI streams (wider compatibility).
 *   type2AVI == false -> Type-1 AVI: interleaved DV stream, one video track.
 *
 * Graph topology (Type-2):
 *   COutputFilter -> DV Splitter -> AVI Mux -> File Sink
 *                                   (video)
 *                                   (audio)
 *
 * Graph topology (Type-1):
 *   COutputFilter -> AVI Mux -> File Sink
 */
class CAVIWriter:public COutputGraph {
public:
	/* m_tmpfile  - Temporary path used while capture is in progress (~ prefix). */
	/* m_filename - Base path for the final renamed file. */
	/* m_dtformat - strftime format string for the date/time suffix. */
	CString m_tmpfile, m_filename, m_dtformat;
	/* m_ndigits  - Minimum digit width for the auto-increment numeric suffix. */
	int m_ndigits;
	/* m_dvtime   - Recording timestamp (from DV SSYB), used in the final filename. */
	time_t m_dvtime;

	/* filename  - Base path for output file (without extension).
	 * dtformat  - strftime date/time suffix format (may be empty).
	 * ndigits   - Minimum digit count for the auto-increment suffix.
	 * tim       - Seed time for date/time suffix (0 = use current time).
	 * type2AVI  - TRUE to write Type-2 AVI (separate audio/video streams).
	 * type      - DV media type negotiated from the source graph. */
	CAVIWriter(LPCSTR filename, LPCSTR dtformat, int ndigits, time_t tim, bool type2AVI, CMediaType *type);
	/* Sends EOS, waits for graph completion, renames temp file to final path. */
	~CAVIWriter();
};

/*
 * CMonitor
 *
 * COutputGraph subclass that renders DV frames to a preview window via the
 * DirectShow video renderer.
 *
 * Because the video renderer can block on WM_PAINT delivery, frames are
 * not pushed directly from the calling thread.  Instead, a dedicated
 * MonitoringThread runs a rate-limited loop:
 *
 *   1. Acquires an allocator buffer from the output pin.
 *   2. Sleeps to throttle the preview to roughly 5 fps (200 ms intervals),
 *      adjusted dynamically to avoid starvation.
 *   3. Exposes the buffer via m_sample so that the next HandleFrame() call
 *      can fill it with a current DV frame.
 *   4. Waits on m_ev for HandleFrame() to signal that data is ready.
 *   5. Delivers the filled sample downstream to the renderer.
 *
 * If the queue is more than half full (system under load) MonitoringThread
 * skips frames so that the capture/recording thread can keep up.
 *
 * The video window is embedded in m_hWnd as a WS_CHILD subwindow and
 * letterboxed to preserve the 4:3 DV aspect ratio via Resize().
 *
 * DV decoding resolution is set to half-D1 (360x240) for efficiency;
 * full resolution (720x480) is only used when the application requests it
 * via SetDVDecoding().
 */
class CMonitor:public COutputGraph {
public:
	/* Parent window that hosts the video renderer subwindow. */
	HWND m_hWnd;
	/* IVideoWindow interface used to position and size the renderer. */
	IVideoWindow *m_VW;
	/* Buffer slot shared between MonitoringThread and HandleFrame().
	 * MonitoringThread sets this; HandleFrame() fills and clears it. */
	IMediaSample *m_sample;
	/* Background thread that drives the throttled preview loop. */
	CWinThread *m_thread;
	/* Signaled by HandleFrame() when m_sample has been filled with new data. */
	CEvent m_ev;

	/* hWnd - Window handle of the preview control (child window target).
	 * type - DV media type; passed to COutputGraph and the graph builder. */
	CMonitor(HWND hWnd, CMediaType *type);
	/* Signals m_ev (unblocks MonitoringThread) then waits for it to exit. */
	~CMonitor();

	/* Recalculates and repositions the renderer to maintain 4:3 aspect ratio. */
	void Resize();
	/* Called per-frame; fills m_sample and signals m_ev if a slot is waiting. */
	void HandleFrame(REFERENCE_TIME duration, BYTE *data, int len);
	/* Thread body: rate-limited loop that acquires buffers and delivers frames. */
	void MonitoringThread();
};


/*
 * CDVQueue
 *
 * Thread-safe fixed-capacity ring buffer (circular queue) for raw DV frames.
 *
 * Producer/consumer pattern:
 *   Producer thread (DirectShow callback or CAVIJoiner):
 *     Calls Put() once per frame.  If the ring is full, Put() blocks on
 *     m_evPut (a manual-reset CEvent) until the consumer has read at
 *     least one slot.  This provides backpressure: if the disk cannot
 *     keep up, the FireWire driver's own buffer absorbs the brief stall.
 *
 *   Consumer thread (CapturingThread / RecordingThread):
 *     Calls Get() to retrieve the oldest frame.  If the ring is empty,
 *     Get() blocks on m_evGet until the producer has enqueued a frame.
 *     Returns false immediately if m_end is set and the ring is empty,
 *     signaling end-of-stream to the consumer.
 *
 * End-of-stream:
 *   Put() with data == NULL sets m_end = true and signals both events so
 *   that a blocked consumer wakes and a blocked producer can also exit.
 *
 * Memory layout:
 *   A single contiguous allocation (m_buffers) holds all ring slots as an
 *   array of variable-length Buffer structs:
 *     [ header (duration + len) | data[dataSize] ] × queueSize+1
 *   m_queue[] is an array of pointers into m_buffers for O(1) slot access.
 *   The ring uses one extra slot (queueSize+1) to distinguish full from empty
 *   via the m_load counter (rather than relying on head == tail ambiguity).
 *
 * Synchronization:
 *   m_cs  - CCritSec protects m_head, m_tail, m_load.
 *   m_evGet - signaled by Put() each time a slot is filled.
 *   m_evPut - signaled by Get() each time a slot is freed.
 */
class CDVQueue {
	/* A single ring slot: frame timing metadata followed by the raw frame bytes. */
	struct Buffer {
		REFERENCE_TIME duration;  /* frame duration in 100-ns units */
		int len;                   /* number of valid bytes in data[] */
		BYTE data[1];              /* variable-length frame data (actual size = m_dataSize) */
	};
public:
	/* m_evGet - signaled when a slot transitions from empty to occupied (wakes consumer). */
	/* m_evPut - signaled when a slot transitions from occupied to empty (wakes producer). */
	CEvent m_evPut, m_evGet;
	/* Protects m_head, m_tail, m_load during Put() and Get(). */
	CCritSec m_cs;
	/* Contiguous block holding all ring Buffer structs. */
	BYTE *m_buffers;
	/* Array of pointers into m_buffers; provides O(1) slot indexing. */
	Buffer ** m_queue;
	/* m_dataSize  - Bytes per frame slot (e.g. 144000 for PAL).
	 * m_queueSize - Total number of ring slots (queueSize+1).
	 * m_head      - Index of the next slot to read (consumer position).
	 * m_tail      - Index of the next slot to write (producer position).
	 * m_load      - Number of slots currently occupied. */
	int m_dataSize, m_queueSize, m_head, m_tail, m_load;
	/* Set to true by Put(NULL) to indicate end-of-stream. */
	bool m_end;
	/* queueSize - Maximum number of frames buffered simultaneously.
	 * dataSize  - Maximum bytes per frame. */
	CDVQueue(int queueSize, int dataSize);
	~CDVQueue();
	/* Enqueues one frame; blocks if the ring is full.
	 * data == NULL signals end-of-stream (sets m_end, wakes all waiters). */
	void Put(REFERENCE_TIME duration, BYTE *data, int len);
	/* Dequeues the oldest frame; blocks if the ring is empty.
	 * Returns false when end-of-stream has been signaled and the ring is empty.
	 * *data points into the ring buffer and is valid only until the next Get() call. */
	bool Get(REFERENCE_TIME *duration, BYTE **data, int *len);
	/* Like Get(), but returns false if no frame arrives within timeoutMs.
	 * Used by CapturingThread for end-of-signal auto-detection. */
	bool GetWithTimeout(REFERENCE_TIME *duration, BYTE **data, int *len, DWORD timeoutMs);
};

/*
 * CDV
 *
 * Top-level orchestrator and MFC CStatic subclass.
 *
 * CDV owns the entire pipeline and exposes high-level Capture and Record
 * operations to the UI dialog.  It inherits CStatic so that it can be
 * placed directly on the dialog template as a picture control that also
 * hosts the preview window.
 *
 * State machine:
 *   Idle
 *     -> BuildCapturing()  -> CapturePaused
 *     -> BuildRecording()  -> RecordPaused
 *
 *   CapturePaused
 *     -> StartCapturing()  -> Capturing
 *     -> Destroy()         -> Idle
 *
 *   Capturing
 *     -> StopCapturing()   -> CapturePaused
 *     -> (time limit hit)  -> Finished
 *     -> Destroy()         -> Idle
 *
 *   RecordPaused
 *     -> StartRecording()  -> Recording
 *     -> Destroy()         -> Idle
 *
 *   Recording
 *     -> StopRecording()   -> RecordPaused
 *     -> (all files played)-> Finished
 *     -> Destroy()         -> Idle
 *
 *   Finished
 *     -> Destroy()         -> Idle
 *
 * CDV::HandleFrame() is the CFrameHandler implementation: it enqueues
 * every incoming DV frame into m_queue.  CapturingThread / RecordingThread
 * drain the queue and route frames to disk and the preview monitor.
 *
 * Thread safety:
 *   m_captureFilename is protected by m_cs.
 *   m_state transitions are made on the UI thread or on the worker threads
 *   at well-defined synchronization points.
 */
/* Statistics collected during a capture session, written to a CSV log
 * when capture ends or a file is split. */
struct CaptureStats {
	CString sFilename;        /* output AVI filename */
	DWORD   dwStartTick;      /* GetTickCount() at capture start */
	time_t  tFirstRecTime;    /* first valid DV recording timestamp */
	time_t  tLastRecTime;     /* last valid DV recording timestamp */
	DWORD   dwFrameCount;     /* total frames written */
	DWORD   dwDroppedFrames;  /* driver-reported dropped frames */
	BOOL    bIsPAL;           /* TRUE=PAL (144000), FALSE=NTSC (120000) */
	int     eStopReason;      /* 0=USER, 1=SIGNAL_LOST, 2=LOW_DISK, 3=TIMED */
};

/* Appends one log entry to the capture CSV. Creates the file with
 * a header row if it does not yet exist. */
void WriteCaptureLog(LPCSTR szLogPath, const CaptureStats& stats);

class CDV:public CStatic, CFrameHandler  {
public:
	enum {Idle, RecordPaused, Recording, CapturePaused, Capturing, Finished} m_state;
	/* TRUE = write Type-2 AVI (separate audio/video); FALSE = Type-1 (interleaved). */
	bool m_type2AVI;
	/* DV timestamp delta (seconds) that triggers a new split file; 0 = disabled. */
	UINT m_discontinuityTreshold;
	/* Frame count at which the current AVI file is closed and a new one started. */
	UINT m_maxAVIFrames;
	/* Record/capture only every Nth frame (1 = all frames, 2 = half rate, etc.). */
	UINT m_everyNth;
	/* TRUE = show preview during recording (consumes extra queue bandwidth). */
	bool m_recordPreview;
	/* TRUE = send transport-control commands to the DV device automatically. */
	bool m_DVctrl;
	/* Timeout in milliseconds for end-of-signal auto-detection during capture.
	 * If no frames arrive within this period, capture stops automatically.
	 * 0 = disabled (wait indefinitely). Default: 5000 ms. */
	DWORD m_autoStopTimeout;

	CDV();
	~CDV();

	/* Returns the current state enum value. */
	int GetState();
	/* Returns the number of dropped frames since capture started. */
	int GetDropped();
	/* Returns the current queue fill level (number of buffered frames). */
	int GetQueueLoad();
	/* Returns the total number of frames written to disk in this session. */
	long GetCounter();
	/* Returns total accumulated frame duration written (100-ns units). */
	REFERENCE_TIME GetTime();
	/* Returns the filename of the currently open AVI file (thread-safe). */
	CString GetCaptureFilename();

	/* Tears down the entire pipeline and resets state to Idle. */
	void Destroy();

	/* Builds the capture pipeline for the named FireWire source device.
	 * vsrc - Friendly device name (from GetVideoSrcList). */
	void BuildCapturing(LPCSTR vsrc);
	/* Transitions from CapturePaused to Capturing, opening a new AVI writer.
	 * filename    - Base path for output AVI file(s).
	 * dtformat    - strftime suffix format (may be empty).
	 * ndigits     - Minimum digit count for auto-increment numeric suffix.
	 * captureTime - Optional recording duration limit (100-ns units; 0 = unlimited). */
	void StartCapturing(LPCSTR filename, LPCSTR dtformat, int ndigits, REFERENCE_TIME captureTime = 0);
	/* Transitions from Capturing to CapturePaused, closing the current AVI file. */
	void StopCapturing();
	/* Builds the record pipeline from a file list to the named FireWire output device.
	 * filenames - Pipe-separated list of AVI paths / glob patterns.
	 * vdst      - Friendly device name (from GetVideoDstList). */
	void BuildRecording(LPCSTR filenames, LPCSTR vdst);
	/* Transitions from RecordPaused to Recording. */
	void StartRecording();
	/* Transitions from Recording to RecordPaused. */
	void StopRecording();

	/* Thread body for AVI-to-DV recording; drains CDVQueue into CDVOutput. */
	void RecordingThread();
	/* Thread body for DV-to-AVI capture; drains CDVQueue into CAVIWriter(s). */
	void CapturingThread();
protected:
	/* Pipeline components — all owned by CDV, created/destroyed per session. */
	CAVIJoiner *m_aviJoiner;   /* source for recording mode */
	CAVIWriter *m_aviWriter;   /* sink for capture mode (replaced on split) */
	CDVInput * m_dvInput;      /* FireWire capture source */
	CDVOutput * m_dvOutput;    /* FireWire record sink */
	CMonitor *m_monitor;       /* preview window renderer */
	CDVQueue * m_queue;        /* inter-thread frame buffer */

	/* Protects m_captureFilename for cross-thread access. */
	CCritSec m_cs;

	/* The single worker thread (CapturingThread or RecordingThread). */
	CWinThread *m_thread;
	/* Base filename passed to StartCapturing(); used when opening new split files. */
	CString m_captureFilename, m_dtformat;
	int m_ndigits;
	/* Cumulative dropped-frame count (updated each frame in CapturingThread). */
	long m_dropped;
	/* Total frames written to disk in the current capture/record session. */
	long m_counter;
	/* Total accumulated presentation time written (100-ns units). */
	REFERENCE_TIME m_time;
	/* If non-zero, capture stops automatically after this many 100-ns units. */
	REFERENCE_TIME m_captureTime;

	/* CFrameHandler implementation: enqueues every incoming frame into m_queue. */
	void HandleFrame(REFERENCE_TIME duration, BYTE *data, int len);

	//{{AFX_MSG(CDV)
	afx_msg void OnSize(UINT nType, int cx, int cy);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

/* Fills list with friendly names of all available video input (capture) devices. */
void GetVideoSrcList(CArray<CString,CString&> &);
/* Fills list with friendly names of all available video output (render) devices. */
void GetVideoDstList(CArray<CString,CString&> &);

/* Formats tim using strftime with the given format string.
 * Returns an empty string if format is empty. */
CString FormatTime(LPCSTR format, time_t tim);
