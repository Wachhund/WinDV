#include "stdafx.h"
#include <process.h>
#include "DShow.h"
#include "DV.h"
#include "sha256.h"

/////////////////////////////////////////////////////////////////////////////
// Utility / helpers
/////////////////////////////////////////////////////////////////////////////

/*
 * CHECK_HR (inline function)
 *
 * Tests a DirectShow HRESULT and throws CDShowException on failure.
 * Used throughout DShow.cpp as a lightweight error-propagation mechanism
 * instead of long if/goto chains.
 *
 * Parameters:
 *   hr      - HRESULT to test; only S_OK (== 0) is treated as success.
 *   message - Human-readable error description surfaced to the UI.
 *   cause   - CDShowException cause code (error or deviceNotFound).
 */
inline void CHECK_HR(HRESULT hr, LPCSTR message = "Error", int cause = CDShowException::error)
{
	if (hr != S_OK) ThrowDShowException(cause, message);
}

/*
 * SetDVDecoding
 *
 * Adjusts the DV Video Decoder filter's output resolution via IIPDVDec.
 * Called after building the preview graph to select half-D1 (360x240) for
 * efficiency, saving CPU compared to full-D1 (720x480) decoding which would
 * be wasted on a small preview window.
 *
 * The function is intentionally tolerant of missing interfaces: if the
 * "DV Video Decoder" filter is not present in the graph (e.g. Type-1 AVI
 * playback through a different decode path) it simply returns without error.
 *
 * Parameters:
 *   pFG  - The filter graph to search.
 *   full - 0 = half-D1 (360x240 preview), 1 = full-D1 (720x480).
 */
static void SetDVDecoding(IGraphBuilder *pFG, int full = 0)
{
	IBaseFilter *pDVFilt;
	IIPDVDec *pDVDec;
	HRESULT hr;

	/* Find the system DV decoder filter by its well-known name. */
	hr = pFG->FindFilterByName(L"DV Video Decoder", &pDVFilt);
	if (hr == S_OK) {
		hr = pDVFilt->QueryInterface(IID_IIPDVDec, (void**)&pDVDec);
		if (hr == S_OK) {
			/* Choose half-D1 for preview (saves CPU); full-D1 for export. */
			pDVDec->put_IPDisplay(full ? DVDECODERRESOLUTION_720x480 : DVDECODERRESOLUTION_360x240);
		}
		pDVFilt->Release();
	}
}


/*
 * EnumVideoDevices
 *
 * Enumerates all video input devices registered under
 * CLSID_VideoInputDeviceCategory using the DirectShow System Device
 * Enumerator (ICreateDevEnum / IEnumMoniker).
 *
 * The function serves two purposes depending on the arguments:
 *   1. List mode (device == NULL, pfilter == NULL):
 *      Populates list with the friendly names of all video devices.
 *      Used by GetVideoSrcList() and GetVideoDstList() to populate the UI.
 *
 *   2. Bind mode (device != NULL, pfilter != NULL):
 *      Additionally instantiates the first device whose friendly name
 *      matches device and returns its IBaseFilter* in *pfilter.
 *      Throws CDShowException(deviceNotFound) if no match is found.
 *
 * Parameters:
 *   record  - Unused; reserved for future input/output distinction.
 *   device  - Friendly name to match for binding (NULL = list-only).
 *   list    - Receives the friendly name of every enumerated device.
 *   pfilter - Receives the IBaseFilter* for the matched device (or NULL).
 *
 * Throws:
 *   CDShowException::error        - CoCreateInstance or IPropertyBag failure.
 *   CDShowException::deviceNotFound - No video devices, or named device absent.
 */
static void EnumVideoDevices(BOOL record, LPCSTR device, CArray<CString,CString &> &list, IBaseFilter ** pfilter)
{
	HRESULT hr;

	if (pfilter) *pfilter = NULL;

	/* Create the system device enumerator COM object. */
    ICreateDevEnum *pCreateDevEnum;
    hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC,
			  IID_ICreateDevEnum, (void**)&pCreateDevEnum);
    CHECK_HR(hr);

	/* Request an enumerator for video capture devices. */
    IEnumMoniker *pEm;
    hr = pCreateDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory,
								&pEm, 0);
    pCreateDevEnum->Release();
	CHECK_HR(hr, "No video device found", CDShowException::deviceNotFound);
    pEm->Reset();
    ULONG cFetched;
    IMoniker *pM;
    while(hr = pEm->Next(1, &pM, &cFetched), hr==S_OK)
    {
	    IPropertyBag *pBag;
		/* Bind to the device's property bag to read its friendly name. */
	    hr = pM->BindToStorage(0, 0, IID_IPropertyBag, (void **)&pBag);
	    if(SUCCEEDED(hr)) {
			VARIANT varName;
			varName.vt = VT_BSTR;
			hr = pBag->Read(L"FriendlyName", &varName, NULL);
			CHECK_HR(hr);
			pBag->Release();

			/* Filter for DV devices only: bind the device and check whether
			 * any output pin supports MEDIATYPE_Interleaved (the DV media type).
			 * Non-DV devices (webcams, USB capture cards) are skipped. */
			IBaseFilter *pTestFilter = NULL;
			hr = pM->BindToObject(NULL, NULL, IID_IBaseFilter, (void **)&pTestFilter);
			if (SUCCEEDED(hr) && pTestFilter) {
				bool isDV = false;
				IEnumPins *pPinEnum = NULL;
				if (SUCCEEDED(pTestFilter->EnumPins(&pPinEnum))) {
					IPin *pPin;
					while (pPinEnum->Next(1, &pPin, NULL) == S_OK) {
						IEnumMediaTypes *pMTEnum = NULL;
						if (SUCCEEDED(pPin->EnumMediaTypes(&pMTEnum))) {
							AM_MEDIA_TYPE *pMT;
							while (pMTEnum->Next(1, &pMT, NULL) == S_OK) {
								if (pMT->majortype == MEDIATYPE_Interleaved) {
									isDV = true;
								}
								DeleteMediaType(pMT);
								if (isDV) break;
							}
							pMTEnum->Release();
						}
						pPin->Release();
						if (isDV) break;
					}
					pPinEnum->Release();
				}

				if (isDV) {
					CString tmp = varName.bstrVal;
					list.Add(tmp);
					/* If a specific device name was requested and not yet bound, keep the filter. */
					if (device && pfilter && tmp == device && !*pfilter) {
						*pfilter = pTestFilter;
						pTestFilter = NULL; /* prevent release below */
					}
				}
				if (pTestFilter) pTestFilter->Release();
			}

			SysFreeString(varName.bstrVal);
		}
		pM->Release();
    }
    pEm->Release();

	/* If a specific device was requested but not found in the enumeration, throw. */
	if (pfilter && !*pfilter) ThrowDShowException(CDShowException::deviceNotFound, "Device not found");
}

/* Populates list with friendly names of all video capture (input) devices. */
void GetVideoSrcList(CArray<CString,CString &> &list)
{
	EnumVideoDevices(FALSE, NULL, list, NULL);
}

/* Populates list with friendly names of all video render (output) devices. */
void GetVideoDstList(CArray<CString,CString &> &list)
{
	EnumVideoDevices(TRUE, NULL, list, NULL);
}

/*
 * GetCaptureFilename  (file-scope helper)
 *
 * Generates a unique AVI output filename from a base path, an optional
 * date/time suffix, and an auto-incrementing numeric suffix.
 *
 * Naming scheme:  <base>[.<datetime>][.<NNN>].avi
 *
 * The function scans for existing files matching the pattern
 *   <base>[.<datetime>].*.avi
 * and picks the next available numeric suffix so that the returned path
 * does not collide with any existing file.
 *
 * Parameters:
 *   base      - Base file path (without extension).
 *   dtformat  - strftime format for the date/time component (empty = omit).
 *   ndigits   - Minimum numeric suffix width (0 = no suffix unless collision).
 *   tim       - Time used for the date/time suffix (0 = current time).
 *
 * Returns:
 *   A path string of the form <base>[.<datetime>][.<NNN>].avi
 *   that does not yet exist on the filesystem.
 */
static CString GetCaptureFilename(LPCSTR base, LPCSTR dtformat, int ndigits, time_t tim)
{
	CString basdattim = base;
	if (tim <= 0) tim = time(NULL);

	/* Append the formatted date/time suffix if a format string was provided. */
	CString tmp = FormatTime(dtformat, tim);
	if (!tmp.IsEmpty()) {
		basdattim += ".";
		basdattim += tmp;
	}
	tmp = basdattim;

	/* Calculate the length of the base+datetime component (after the last
	 * path separator) so we can strip it when comparing existing filenames. */
	int bassiz = 0;
	int i = basdattim.GetLength();
	while (i) {
		i--;
		switch (basdattim[i]) {
		case '\\':
		case '/':
		case ':':
			goto endwhile;
		}
		bassiz++;
	}
endwhile:

	/* Build the wildcard pattern to find all existing split-file candidates. */
	tmp += ".*.avi";

	int max = -1;
	int maxn = ndigits;

	/* Scan existing files to find the highest numeric suffix already in use. */
	CFileFind ff;
	BOOL found = ff.FindFile(tmp);
	while (found) {
		found = ff.FindNextFile();
		tmp = ff.GetFileName();
		/* Extract the numeric part between the base and ".avi". */
		int l = tmp.GetLength() - bassiz - 5;  /* 5 = len(".") + len(".avi") */
		if (l > 0 && l >= maxn) {
			tmp = tmp.Mid(bassiz+1,l);
			int j = 0;
			for(i=0; i<l; i++) {
				if (!isdigit(tmp[i])) goto next;
				j = j*10 + (tmp[i]-'0');
			}
			/* Keep track of the highest numeric suffix found. */
			if (l>maxn) {
				maxn = l; max = j;
			}
			else if (j > max) max = j;
		}
next:;
	}
	ff.Close();

	tmp = basdattim;

	/* Format the next suffix as zero-padded to at least maxn digits. */
	CString num;
	num.Format(".%0*d", maxn, max+1);

	/* If ndigits is 0 and no numeric suffix is needed, check whether the
	 * plain .avi name already exists before deciding to add a suffix. */
	if (!maxn) {
		if (ff.FindFile(basdattim+".avi")) maxn++;

		ff.Close();

	}

	if (maxn) tmp += num;

	return tmp + ".avi";
}

/////////////////////////////////////////////////////////////////////////////
// CDShowException
/////////////////////////////////////////////////////////////////////////////

CDShowException::CDShowException(BOOL b_AutoDelete, int cause, LPCSTR message)
: CException(b_AutoDelete)
{
	m_cause = cause;
	m_message = message;
}

/* Required by MFC CException protocol; copies m_message into the caller's buffer. */
BOOL CDShowException::GetErrorMessage(LPTSTR lpszError, UINT nMaxError, PUINT pnHelpContext)
{
	lstrcpyn(lpszError, m_message, nMaxError);
	if (pnHelpContext) *pnHelpContext = 0;
	return TRUE;
}

/* Allocates a self-deleting CDShowException and throws it via MFC THROW. */
void ThrowDShowException(int cause, LPCSTR message)
{
	THROW(new CDShowException(TRUE, cause, message));
}

/////////////////////////////////////////////////////////////////////////////
// CFilterGraph
/////////////////////////////////////////////////////////////////////////////

/*
 * CFilterGraph constructor
 *
 * Creates and wires the four core DirectShow COM objects that every graph
 * variant (input or output) requires:
 *
 *   1. ICaptureGraphBuilder2  — high-level helper for building capture
 *      topologies; connected to m_FG so it can add and connect filters.
 *   2. IGraphBuilder          — the filter graph manager; owns all filters
 *      and manages the data-flow graph.
 *   3. IMediaControl          — exposes Run / Pause / Stop on the graph.
 *   4. IMediaSeeking          — exposes duration and position queries.
 *   5. IMediaEventEx          — delivers EC_COMPLETE and other graph events,
 *      used by CAVIWriter and CDVOutput to wait for graph completion.
 *
 * All interfaces are queried from the filter graph (m_FG) so they share
 * the same underlying COM object lifetime.
 */
CFilterGraph::CFilterGraph()
{
	HRESULT hr;

	/* Create the capture graph builder and associate it with a new filter graph. */
	hr = CoCreateInstance((REFCLSID)CLSID_CaptureGraphBuilder2,
				  NULL, CLSCTX_INPROC, (REFIID)IID_ICaptureGraphBuilder2,
				  (void **)&m_GB);
	CHECK_HR(hr, "Can't create CaptureGraphBuilder");

	hr = CoCreateInstance((REFCLSID)CLSID_FilterGraph,
				  NULL, CLSCTX_INPROC, (REFIID)IID_IGraphBuilder,
				  (void **)&m_FG);
	CHECK_HR(hr, "Can't create FilterGraph");

	/* Bind the builder to the filter graph so RenderStream() operates on m_FG. */
	hr = m_GB->SetFiltergraph(m_FG);
	CHECK_HR(hr);

	/* Query the three control interfaces from the same underlying graph object. */
	hr = m_FG->QueryInterface(IID_IMediaControl, (void **)&m_MC);
	CHECK_HR(hr);

	hr = m_FG->QueryInterface(IID_IMediaSeeking, (void **)&m_MS);
	CHECK_HR(hr);

	hr = m_FG->QueryInterface(IID_IMediaEventEx, (void **)&m_ME);
	CHECK_HR(hr);
}

/*
 * CFilterGraph destructor
 *
 * Stops the graph before releasing interfaces to avoid delivering frames
 * after the derived-class pipeline objects have been destroyed.
 * Release order mirrors construction order (reverse dependency).
 */
CFilterGraph::~CFilterGraph()
{
	if (m_MC) {m_MC->Stop(); m_MC->Release();}
	if (m_MS) m_MS->Release();
	if (m_ME) m_ME->Release();
	if (m_FG) m_FG->Release();
	if (m_GB) m_GB->Release();
}

/////////////////////////////////////////////////////////////////////////////
// CInputGraph
/////////////////////////////////////////////////////////////////////////////

/*
 * CInputGraph constructor
 *
 * Creates the custom sink filter (CInputFilter) and registers it in the
 * filter graph under the name "InputFilter".  Subclass constructors
 * (CAVIReader, CDVInput) then build the upstream portion of the graph and
 * connect it to m_inputFilter->m_input.
 */
CInputGraph::CInputGraph()
: m_handler(NULL)
{
	HRESULT hr;

	/* Construct the sink filter with an initial ref-count of 1 via AddRef(). */
	m_inputFilter = new CInputFilter(this); m_inputFilter->AddRef();

	hr = m_FG->AddFilter(m_inputFilter, L"InputFilter");

}

/*
 * CInputGraph destructor
 *
 * Stops the graph first, then nulls the back-pointer in CInputFilter before
 * releasing it.  Nulling m_graph prevents any in-flight Receive() callbacks
 * from dereferencing a destroyed CInputGraph after the Release().
 */
CInputGraph::~CInputGraph()
{
	Stop();
	m_inputFilter->m_graph = NULL;
	m_inputFilter->Release();
}

/*
 * CInputGraph::Run
 *
 * Sets the frame handler and starts the DirectShow graph.  From this point
 * frames arrive on the DirectShow streaming thread via CInputPin::Receive().
 */
void CInputGraph::Run(CFrameHandler *handler)
{
	m_handler = handler;
	HRESULT hr = m_MC->Run();
	CHECK_HR(hr, "Can't start input graph");
}

/* Stops the graph and clears the handler reference. */
void CInputGraph::Stop()
{
	if (m_MC) m_MC->Stop();
	m_handler = NULL;
}

/*
 * CInputGraph::GetMediaType
 *
 * Retrieves the media type that was negotiated when the upstream source
 * connected to CInputPin.  The sample size is clamped to at least 144000
 * bytes (PAL frame size) so that CDVQueue and CAVIWriter always allocate
 * enough space for both NTSC and PAL frames regardless of what the source
 * reported during negotiation.
 */
void CInputGraph::GetMediaType(CMediaType *type)
{
	AM_MEDIA_TYPE mt;
	/* Read the media type from the connected pin negotiation. */
	HRESULT hr = m_inputFilter->m_input->ConnectionMediaType(&mt);
	CHECK_HR(hr, "Can't get input media type");
	*type = mt;
	DVINFO *dvinfo = (DVINFO *)mt.pbFormat;
	FreeMediaType(mt);
	/* Ensure the sample size covers the largest possible DV frame (PAL). */
	long l = type->GetSampleSize();
	if (l < 144000) type->SetSampleSize(144000);
}


/*
 * CInputFilter constructor
 *
 * Passes CLSID_NULL as the filter CLSID because this filter is never
 * registered in the system; it exists solely within this process's graph.
 * The CCritSec member is passed to CBaseFilter to serialise state changes.
 */
CInputGraph::CInputFilter::CInputFilter(CInputGraph *graph)
: CBaseFilter(NAME("DV Destination"), NULL, &m_cs, (REFIID)CLSID_NULL), m_graph(graph)
{
	HRESULT hr = NOERROR;
	m_input = new CInputPin(this, &m_cs, &hr);
}

CInputGraph::CInputFilter::~CInputFilter()
{
	delete m_input;
}

/* CBaseFilter requires GetPinCount(); this filter has exactly one pin. */
int CInputGraph::CInputFilter::GetPinCount()
{
	return 1;
}

/* Returns the single input pin for index 0; required by CBaseFilter. */
CBasePin *CInputGraph::CInputFilter::GetPin(int n)
{
	return m_input;
}

/*
 * CInputPin constructor
 *
 * Registers the pin with name "Input" (ANSI for the debug name, L"Input"
 * for the pin ID used by the graph builder when connecting pins).
 */
CInputGraph::CInputFilter::CInputPin::CInputPin(CInputFilter *pFilter, CCritSec *cs, HRESULT *phr)
: CBaseInputPin(NAME("Input"), pFilter, cs, phr, L"Input")
{
}

/*
 * CInputPin::CheckMediaType
 *
 * Accepts any stream typed as MEDIATYPE_Interleaved (the DV interleaved
 * container type used by both Type-1 AVI and raw FireWire capture).
 * All other major types are rejected with S_FALSE.
 */
HRESULT CInputGraph::CInputFilter::CInputPin::CheckMediaType(const CMediaType *pmt)
{
	if (*pmt->Type() == MEDIATYPE_Interleaved) return S_OK;
	return S_FALSE;
}

/*
 * CInputPin::Receive  (hot path, ~25-30 calls/second)
 *
 * Called by DirectShow's streaming thread for every delivered IMediaSample.
 * Extracts the frame duration from the sample timestamps and the raw bytes,
 * then forwards both to the CInputGraph's registered CFrameHandler.
 *
 * The base class Receive() checks for flushing/EOS state before we proceed.
 */
STDMETHODIMP CInputGraph::CInputFilter::CInputPin::Receive(IMediaSample *pSample)
{
	HRESULT hr = CBaseInputPin::Receive(pSample);
	if (hr != NOERROR) return hr;

	REFERENCE_TIME startTime, endTime;
	pSample->GetTime(&startTime, &endTime);
	BYTE *ptr;
	pSample->GetPointer(&ptr);
	long len = pSample->GetActualDataLength();
	/* Forward the frame duration and raw bytes to the registered handler. */
	((CInputFilter *)m_pFilter)->m_graph->m_handler->HandleFrame(endTime - startTime, ptr, len);
	return hr;
}

/*
 * CInputPin::EndOfStream
 *
 * Called by DirectShow when the upstream source has no more data.
 * Signals EOS to the handler by passing duration=-1, data=NULL, len=0.
 * CDVQueue::Put() and CDV::CapturingThread() use this sentinel to
 * detect end-of-file and perform clean shutdown.
 */
STDMETHODIMP CInputGraph::CInputFilter::CInputPin::EndOfStream()
{
	((CInputFilter *)m_pFilter)->m_graph->m_handler->HandleFrame(-1, NULL, 0);
	return NOERROR;
}

/////////////////////////////////////////////////////////////////////////////
// COutputGraph
/////////////////////////////////////////////////////////////////////////////

/*
 * COutputGraph constructor
 *
 * Creates the custom source filter (COutputFilter) and registers it in the
 * filter graph under the name "OutputFilter".  Subclass constructors
 * (CAVIWriter, CDVOutput, CMonitor) then build the downstream portion of
 * the graph and connect it to m_outputFilter->m_output.
 *
 * Parameters:
 *   type  - DV media type; copied for use during pin media-type negotiation.
 *   queue - Async queue depth (0 = synchronous; >0 = dedicated delivery thread).
 */
COutputGraph::COutputGraph(CMediaType *type, int queue)
: m_time(0), m_type(*type), m_queue(queue)
{
	HRESULT hr;

	m_outputFilter = new COutputFilter(this); m_outputFilter->AddRef();

	hr = m_FG->AddFilter(m_outputFilter, L"OutputFilter");
}

/* Internal struct used to carry frame parameters; retained for potential future use. */
struct FrameInfo {
	REFERENCE_TIME duration;
	BYTE *data;
	int len;
};


/*
 * COutputGraph::HandleFrame
 *
 * Packages raw DV frame data into an IMediaSample and delivers it through
 * the filter graph to the downstream sink (AVI mux, DV device, or renderer).
 *
 * Presentation timestamps are maintained via m_time, which is advanced by
 * each frame's duration so that the downstream filter receives a monotonically
 * increasing time line.  duration == 0 suppresses timestamp setting (used
 * when the source does not provide timing information).
 *
 * A NULL data pointer (end-of-stream sentinel) is silently ignored here;
 * callers use DeliverEndOfStream() separately for EOS signaling.
 */
void COutputGraph::HandleFrame(REFERENCE_TIME duration, BYTE *data, int len)
{
	if (data) {
		IMediaSample *pSample = NULL;
		HRESULT hr;

		/* Acquire an allocator buffer; blocks if all buffers are in use. */
		hr = m_outputFilter->m_output->GetDeliveryBuffer(&pSample, NULL, NULL, 0);
		CHECK_HR(hr);

		BYTE *ptr;

		hr = pSample->GetPointer(&ptr);
		if (FAILED(hr) || !ptr) {
			pSample->Release();
			return;
		}

		CopyMemory(ptr, data, len);
		pSample->SetActualDataLength(len);

		/* Set the presentation time range [m_time, m_time+duration].
		 * Skip if duration is zero to avoid confusing the downstream filter. */
		REFERENCE_TIME newTime = m_time + duration;
		if (duration) pSample->SetTime(&m_time, &newTime);
		m_time = newTime;
		/* Mark every DV frame as a sync point (all DV frames are independently decodable). */
		pSample->SetSyncPoint(true);

		hr = m_outputFilter->m_output->Deliver(pSample);
		if (FAILED(hr)) {
			TRACE("COutputGraph::HandleFrame: Deliver failed, hr=0x%08x\n", hr);
		}

		pSample->Release();
	}
}


COutputGraph::~COutputGraph()
{
	m_outputFilter->Release();
}


/*
 * COutputFilter constructor
 *
 * Registers with CLSID_NULL (unregistered in-process filter).
 * Creates the single output pin which will be connected by the graph builder.
 */
COutputGraph::COutputFilter::COutputFilter(COutputGraph *graph)
: CBaseFilter(NAME("DV Source"), NULL, &m_cs, (REFIID)CLSID_NULL), m_graph(graph)
{
	HRESULT hr = NOERROR;
	m_output = new COutputPin(this, &m_cs, &hr);
}

COutputGraph::COutputFilter::~COutputFilter()
{
	delete m_output;
}

/* CBaseFilter requires GetPinCount(); this filter has exactly one pin. */
int COutputGraph::COutputFilter::GetPinCount()
{
	return 1;
}

/* Returns the single output pin for index 0; required by CBaseFilter. */
CBasePin *COutputGraph::COutputFilter::GetPin(int n)
{
	return m_output;
}

/*
 * COutputPin constructor
 *
 * m_queue starts NULL; it is created in Active() when an async queue is
 * requested and destroyed in Inactive().
 */
COutputGraph::COutputFilter::COutputPin::COutputPin(COutputFilter *pFilter, CCritSec *cs, HRESULT *phr)
: CBaseOutputPin(NAME("Output"), pFilter, cs, phr, L"Output"), m_queue(NULL)
{
}

/*
 * COutputPin::GetMediaType
 *
 * Reports the single supported media type (m_graph->m_type).
 * iPosition > 0 returns VFW_S_NO_MORE_ITEMS to tell the graph builder
 * that only one type is available.
 */
HRESULT COutputGraph::COutputFilter::COutputPin::GetMediaType(int iPosition, CMediaType *pmt)
{
	if (iPosition > 0) return VFW_S_NO_MORE_ITEMS;
	*pmt = (((COutputFilter*)m_pFilter)->m_graph->m_type);
	return S_OK;
}

/*
 * COutputPin::CheckMediaType
 *
 * Accepts only the exact type that was configured at graph construction.
 * Any other type is rejected with S_FALSE so the graph builder tries the
 * next downstream candidate.
 */
HRESULT COutputGraph::COutputFilter::COutputPin::CheckMediaType(const CMediaType *pmt)
{
	if (*pmt != (((COutputFilter*)m_pFilter)->m_graph->m_type)) return S_FALSE;
	return S_OK;
}


/*
 * COutputPin::DecideBufferSize
 *
 * Negotiates allocator properties with the downstream filter.
 * Enforces minimum constraints:
 *   - cbAlign  >= 1       (no alignment requirement from us)
 *   - cbBuffer >= lSampleSize from our media type (one full DV frame)
 *   - cBuffers >= m_queue (must have at least as many buffers as queue slots,
 *                          or 1 if synchronous delivery is used)
 */
HRESULT COutputGraph::COutputFilter::COutputPin::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *ppropInputRequest)
{
	ALLOCATOR_PROPERTIES actual;
	CMediaType mt = ((COutputFilter*)m_pFilter)->m_graph->m_type;
	if (ppropInputRequest->cbAlign < 1) ppropInputRequest->cbAlign = 1;
	/* Ensure each buffer is large enough to hold one complete DV frame. */
	if ((unsigned)ppropInputRequest->cbBuffer < (unsigned)mt.lSampleSize) ppropInputRequest->cbBuffer = mt.lSampleSize;
	int queue = ((COutputFilter*)m_pFilter)->m_graph->m_queue;
	queue = queue > 1 ? queue : 1;
	/* Allocate at least as many buffers as the async queue depth. */
	if (ppropInputRequest->cBuffers<queue) ppropInputRequest->cBuffers = queue;
	pAlloc->SetProperties(ppropInputRequest, &actual);
	return S_OK;
}

/*
 * COutputPin::Deliver
 *
 * Routes the sample through COutputQueue when async delivery is active
 * (m_queue != NULL), which means delivery happens on COutputQueue's
 * internal thread rather than the caller's thread.  Falls back to
 * synchronous CBaseOutputPin::Deliver() when no queue is present.
 *
 * Note: pSample->AddRef() is called before handing off to COutputQueue
 * because COutputQueue takes ownership and will Release() it.
 */
HRESULT COutputGraph::COutputFilter::COutputPin::Deliver(IMediaSample *pSample)
{
	if (m_queue) {
		pSample->AddRef();
		return m_queue->Receive(pSample);
	}
	else
		return CBaseOutputPin::Deliver(pSample);
}

/*
 * COutputPin::DeliverEndOfStream
 *
 * Routes the EOS notification through COutputQueue when active, or
 * falls back to the base class synchronous notification.
 */
HRESULT COutputGraph::COutputFilter::COutputPin::DeliverEndOfStream()
{
	if (m_queue) {
		m_queue->EOS();
		return S_OK;
	}
	else
		return CBaseOutputPin::DeliverEndOfStream();
}

/*
 * COutputPin::Active
 *
 * Called by DirectShow when the graph transitions from Stopped to Running
 * (or Paused).  If an async queue was requested, creates a COutputQueue
 * configured to use a thread at THREAD_PRIORITY_ABOVE_NORMAL.  The queue
 * receives samples from HandleFrame() and delivers them to the downstream
 * filter on its own thread, so the calling thread (e.g. RecordingThread)
 * is not blocked by slow downstream processing.
 *
 * COutputQueue arguments:
 *   GetConnected()          - downstream pin to deliver to
 *   FALSE                   - do not use the calling thread for delivery
 *   TRUE                    - batch mode (queue multiple samples before notifying)
 *   1                       - event object count (unused)
 *   FALSE                   - do not auto-flush on EOS
 *   queue                   - queue depth
 *   THREAD_PRIORITY_ABOVE_NORMAL - delivery thread priority
 */
HRESULT COutputGraph::COutputFilter::COutputPin::Active()
{
	int queue = ((COutputFilter*)m_pFilter)->m_graph->m_queue;
	if (queue > 0) {
		HRESULT hr = S_OK;
		m_queue = new COutputQueue(GetConnected(), &hr, FALSE, TRUE, 1, FALSE, queue, THREAD_PRIORITY_ABOVE_NORMAL);
	}
	return CBaseOutputPin::Active();
}

/*
 * COutputPin::Inactive
 *
 * Called when the graph transitions back to Stopped.
 * Destroys the COutputQueue (which flushes and joins its delivery thread)
 * before calling the base class, ensuring no further deliveries occur.
 */
HRESULT COutputGraph::COutputFilter::COutputPin::Inactive()
{
	if (m_queue) {
		delete m_queue;
		m_queue = NULL;
	}
	return CBaseOutputPin::Inactive();
}

/////////////////////////////////////////////////////////////////////////////
// CAVIReader
/////////////////////////////////////////////////////////////////////////////

/*
 * CAVIReader constructor
 *
 * Builds a filter graph that reads a DV AVI file and delivers interleaved
 * DV frames to CInputFilter.
 *
 * Two graph topologies are attempted in order:
 *
 * 1. Type-1 (interleaved DV):
 *      FileSource -> AVI Splitter --(MEDIATYPE_Interleaved)--> CInputFilter
 *    The AVI Splitter directly exposes an interleaved DV output pin.
 *
 * 2. Type-2 (separate audio + video streams):
 *      FileSource -> AVI Splitter -> DV Mux -> CInputFilter
 *                   (video)       ↗
 *                   (audio)      ↗
 *    If RenderStream for MEDIATYPE_Interleaved fails or leaves CInputPin
 *    unconnected, a DV Mux is inserted to merge the separate streams back
 *    into an interleaved DV stream before delivery.
 *
 * filename is converted from ANSI to Unicode for AddSourceFilter().
 */
CAVIReader::CAVIReader(LPCSTR filename)
{
	HRESULT hr;

	IBaseFilter *pFSRC;
	WCHAR wbuf[256];
	/* Convert ANSI filename to Unicode for the DirectShow API. */
	MultiByteToWideChar(CP_ACP, 0, filename, -1, wbuf, sizeof wbuf/ sizeof (WCHAR));
	hr = m_FG->AddSourceFilter(wbuf, L"File source", &pFSRC);
	CHECK_HR(hr, "Can't open AVI file");

	/* Add the AVI Splitter filter to parse the container. */
	IBaseFilter * pAVI;
	hr = CoCreateInstance((REFCLSID)CLSID_AviSplitter,
						  NULL, CLSCTX_INPROC, (REFIID)IID_IBaseFilter,
						  (void **)&pAVI);
	CHECK_HR(hr, "Can't create AVI Splitter");
	hr = m_FG->AddFilter(pAVI, L"AVI Splitter");
	CHECK_HR(hr);
	/* Connect File Source -> AVI Splitter. */
	hr = m_GB->RenderStream(NULL, NULL, pFSRC, NULL, pAVI);
	pFSRC->Release();
	CHECK_HR(hr);

	/* Try Type-1: connect AVI Splitter's interleaved DV output directly. */
	hr = m_GB->RenderStream(NULL, &MEDIATYPE_Interleaved, pAVI, NULL, m_inputFilter);
	if (hr != NOERROR || !m_inputFilter->m_input->IsConnected()) {
		/* Type-1 failed; attempt Type-2 by inserting a DV Mux to merge streams. */
  	  IBaseFilter * pDVMux;
	  hr = CoCreateInstance((REFCLSID)CLSID_DVMux,
						  NULL, CLSCTX_INPROC, (REFIID)IID_IBaseFilter,
						  (void **)&pDVMux);
	  CHECK_HR(hr, "Can't create DV Mux");
	  hr = m_FG->AddFilter(pDVMux, L"DV muxer");
  	  CHECK_HR(hr);
	  /* Route video and audio streams through DV Mux, then to CInputFilter. */
	  hr = m_GB->RenderStream(NULL, &MEDIATYPE_Video, pAVI, NULL, pDVMux);
	  hr = m_GB->RenderStream(NULL, &MEDIATYPE_Audio, pAVI, NULL, pDVMux);
	  hr = m_GB->RenderStream(NULL, NULL, pDVMux, NULL, m_inputFilter);
	  pDVMux->Release();
	}
	pAVI->Release();
#ifdef DEBUG
	DumpGraph(m_FG, 0);
#endif
}

/////////////////////////////////////////////////////////////////////////////
// CAVIJoiner
/////////////////////////////////////////////////////////////////////////////

/* Static thread-proc trampoline; required because AfxBeginThread takes a plain function. */
static UINT JoinerThread(LPVOID ptr)
{
	((CAVIJoiner *)ptr)->JoinerThread();
	return 0;
}

/* Case-insensitive comparator for qsort() on CString arrays. */
static int CompareStrings(const void *a, const void *b)
{
	return ((CString *)a)->CompareNoCase(*(CString *)b);
}

/*
 * CAVIJoiner constructor
 *
 * Parses the pipe-delimited filenames string, expands any glob wildcards,
 * sorts each glob result lexicographically (so split files play in the
 * correct numbered order), and opens the first file via CAVIReader.
 *
 * Pipe-separated tokens allow the caller to specify multiple independent
 * file groups (e.g. "C:\a\*.avi|C:\b\clip.avi").  Each token is glob-
 * expanded independently; the resulting sorted lists are appended to
 * m_filenames in order.
 *
 * filenames - Pipe-separated list of file paths or glob patterns.
 * Throws CDShowException::error if no files match any of the tokens.
 */
CAVIJoiner::CAVIJoiner(LPCSTR filenames)
: m_joinHandler(NULL), m_current(0), m_stopping(FALSE),
  m_reader(NULL), m_thread(NULL)
{
	CString flnms = filenames;

	while (!flnms.IsEmpty()) {
		/* Split on '|' to get the next filename token. */
		CString filename = flnms.SpanExcluding("|");
		flnms = filename.GetLength() < flnms.GetLength() ? flnms.Mid(filename.GetLength()+1) : "";

		filename.TrimLeft(); filename.TrimRight();

		if (!filename.IsEmpty()) {
			/* Expand wildcards for this token, collecting all matching paths. */
			CArray<CString,CString&> files;
			CFileFind finder;
			BOOL found;
			found = finder.FindFile(filename);
			if (!found) ThrowDShowException(CDShowException::error, filename + ": file not found");
			while (found) {
				found = finder.FindNextFile();
				files.Add(finder.GetFilePath());
			}
			/* Sort so that split files (e.g. clip.001.avi, clip.002.avi) play in order. */
			if (files.GetSize()) {
				CString *ptr = files.GetData();
				qsort(ptr, files.GetSize(), sizeof (CString), CompareStrings);
			}
			for(int i = 0; i<files.GetSize(); i++) {
				m_filenames.Add(files[i]);
			}

		}

	}
	/* Open the first file immediately so GetMediaType() is available before Run(). */
	if (m_current < m_filenames.GetSize()) {
		m_reader = new CAVIReader(m_filenames[m_current]);
		m_current++;
	}
	else {
		ThrowDShowException(CDShowException::error, "No file selected");
	}

}

CAVIJoiner::~CAVIJoiner()
{
	Stop();
	delete m_reader;
}

/* Delegates to the current CAVIReader (the first file is already open). */
void CAVIJoiner::GetMediaType(CMediaType *type)
{
	m_reader->GetMediaType(type);
}

/*
 * CAVIJoiner::Run
 *
 * Starts the background JoinerThread and then starts the first CAVIReader.
 * The thread is created suspended and resumed after m_bAutoDelete = FALSE
 * is set, ensuring we can safely call WaitForSingleObject() and delete it
 * in Stop().
 *
 * The reader is started after the thread so that end-of-stream events
 * arriving on the DirectShow thread are handled by an already-running
 * JoinerThread.
 */
void CAVIJoiner::Run(CFrameHandler *handler)
{
	m_joinHandler = handler;

	m_thread = AfxBeginThread(::JoinerThread,this,THREAD_PRIORITY_NORMAL,0,CREATE_SUSPENDED);
	m_thread->m_bAutoDelete = FALSE;
	m_thread->ResumeThread();

	m_reader->Run(this);
}

/*
 * CAVIJoiner::Stop
 *
 * Signals JoinerThread to terminate, waits for it, then cleans up.
 * m_stopping is set before signaling m_ev to ensure JoinerThread exits
 * its loop rather than opening the next file.
 */
void CAVIJoiner::Stop()
{
	m_stopping = TRUE;
	if (m_thread) {
		/* Wake JoinerThread if it is blocked waiting for end-of-file. */
		m_ev.SetEvent();
		WaitForSingleObject(m_thread->m_hThread, INFINITE);
		delete m_thread;
		m_thread = NULL;
	}
	delete m_reader;
	m_reader = NULL;
	m_stopping = FALSE;
	m_joinHandler = NULL;
}

/*
 * CAVIJoiner::HandleFrame
 *
 * Called by the current CAVIReader on the DirectShow streaming thread.
 *
 * Live frames (data != NULL) are forwarded to m_joinHandler unchanged.
 * End-of-stream (data == NULL) signals m_ev to wake JoinerThread, which
 * will then open and start the next file.
 *
 * m_stopping guard prevents any callbacks after Stop() has been called,
 * avoiding a race between the streaming thread and Stop()'s cleanup.
 */
void CAVIJoiner::HandleFrame(REFERENCE_TIME duration, BYTE *data, int len)
{
	if (m_stopping)
		return;

	if (data) {
		/* Normal frame: forward directly to the downstream handler. */
		m_joinHandler->HandleFrame(duration, data, len);
	}
	else {
		/* End-of-stream from the current file: wake JoinerThread to load the next one. */
		m_ev.SetEvent();
	}
}

/*
 * CAVIJoiner::JoinerThread
 *
 * Background thread that manages the sequential file transition.
 * Waits on m_ev (signaled by HandleFrame on EOS or by Stop()).
 *
 * On each wake:
 *   - If m_stopping: exit immediately.
 *   - If more files remain: replace m_reader with the next file's CAVIReader
 *     and start it.  The new reader will call HandleFrame() again when it
 *     eventually reaches its own end-of-stream, signaling m_ev again.
 *   - If all files are exhausted: send an EOS frame to m_joinHandler
 *     (data=NULL, duration=-1) to trigger orderly shutdown, then exit.
 */
void CAVIJoiner::JoinerThread()
{
	for(;;) {
		/* Block until end-of-stream on the current file (or Stop() is called). */
		CSingleLock lck(&m_ev);
		lck.Lock();

		if (m_stopping) return;

		if (m_current < m_filenames.GetSize()) {
			/* Destroy the finished reader and open the next file. */
			delete m_reader;
			m_reader = new CAVIReader(m_filenames[m_current]);
			m_current++;
			m_reader->Run(this);
		}
		else {
			/* All files played; send EOS to the downstream handler. */
			m_joinHandler->HandleFrame(-1, NULL, 0);
			return;
		}
	}
}

/////////////////////////////////////////////////////////////////////////////
// CDVControl
/////////////////////////////////////////////////////////////////////////////

CDVControl::CDVControl()
: m_ET(NULL)
{
}

CDVControl::~CDVControl()
{
	if (m_ET) m_ET->Release();
}

/*
 * CDVControl::CtrlAttach
 *
 * Queries IAMExtTransport from the device filter's IUnknown.  If the device
 * does not support this interface (e.g. some consumer cameras), m_ET stays
 * NULL and all Ctrl*() calls become no-ops.  The previous interface pointer
 * is released before the new query.
 */
void CDVControl::CtrlAttach(IUnknown *pDev)
{
	HRESULT hr;
	if (m_ET) m_ET->Release();
	m_ET = NULL;
	hr = pDev->QueryInterface(IID_IAMExtTransport, (void**)&m_ET);
}

/* Sends the STOP transport command to the DV device. */
void CDVControl::CtrlStop()
{
	if (m_ET) {
		m_ET->put_Mode(ED_MODE_STOP);
	}
}

/* Sends the PLAY transport command to the DV device. */
void CDVControl::CtrlPlay()
{
	if (m_ET) {
		m_ET->put_Mode(ED_MODE_PLAY);
	}
}

/*
 * CDVControl::CtrlPause
 *
 * Pauses the DV device by first engaging play then immediately freezing.
 * Some DV camcorders require this two-step sequence (PLAY -> FREEZE) to
 * enter a stable pause/still state; sending FREEZE directly may be ignored.
 */
void CDVControl::CtrlPause()
{
	if (m_ET) {
		m_ET->put_Mode(ED_MODE_PLAY);
		m_ET->put_Mode(ED_MODE_FREEZE);
	}
}

/* Sends the RECORD transport command to the DV device. */
void CDVControl::CtrlRecord()
{
	if (m_ET) {
		m_ET->put_Mode(ED_MODE_RECORD);
	}
}

/* Sends the RECORD_FREEZE (record-pause/standby) command to the DV device. */
void CDVControl::CtrlRecPause()
{
	if (m_ET) {
		m_ET->put_Mode(ED_MODE_RECORD_FREEZE);
	}
}

/////////////////////////////////////////////////////////////////////////////
// CDVInput
/////////////////////////////////////////////////////////////////////////////

/*
 * CDVInput constructor
 *
 * Builds the live capture graph:
 *   DV capture filter (FireWire) --(MEDIATYPE_Interleaved)--> CInputFilter
 *
 * After the graph is built:
 *   - CtrlAttach() stores IAMExtTransport so the camcorder can be commanded.
 *   - IAMDroppedFrames is queried from the capture filter's output pin
 *     (connected to CInputPin) so dropped frames can be monitored.
 *
 * The graph is NOT started here; CDV::BuildCapturing() starts it via Run().
 *
 * vsrc - Friendly name of the FireWire capture device.
 */
CDVInput::CDVInput(LPCSTR vsrc)
: m_DF(NULL)
{
	HRESULT hr;
	IBaseFilter *pVSRC;
	CArray<CString,CString&> list;
	/* Enumerate devices and bind the named source to pVSRC. */
	EnumVideoDevices(FALSE, vsrc, list, &pVSRC);
	/* Attach transport control before adding the filter; pVSRC owns the device. */
	CtrlAttach(pVSRC);
	hr = m_FG->AddFilter(pVSRC, L"DVin");
	CHECK_HR(hr);

	/* Connect: capture filter -> CInputFilter via the interleaved DV pin. */
	hr = m_GB->RenderStream(NULL, &MEDIATYPE_Interleaved, pVSRC, NULL, m_inputFilter);
	pVSRC->Release();
	CHECK_HR(hr);

	/* Obtain IAMDroppedFrames from the capture filter's output pin.
	 * We navigate: CInputPin (connected to) -> upstream output pin -> QI. */
	IPin *pPin;
	hr = m_inputFilter->m_input->ConnectedTo(&pPin);
	CHECK_HR(hr, "Can't find DV output pin");
	hr = pPin->QueryInterface(IID_IAMDroppedFrames, (void**)&m_DF);
	pPin->Release();
	CHECK_HR(hr, "Can't find IAMDroppedFrames");

#ifdef DEBUG
	DumpGraph(m_FG, 0);
#endif
}

CDVInput::~CDVInput()
{
	if (m_DF) m_DF->Release();
}

/*
 * CDVInput::GetDroppedFrames
 *
 * Returns the cumulative count of frames that the FireWire driver has
 * dropped since the graph was started.  A return value of -1 indicates
 * that the interface call failed.
 */
long CDVInput::GetDroppedFrames()
{
	long dropped = -1;
	m_DF->GetNumDropped(&dropped);
	return dropped;
}

/////////////////////////////////////////////////////////////////////////////
// CDVOutput
/////////////////////////////////////////////////////////////////////////////

/*
 * CDVOutput constructor
 *
 * Builds the DV record graph:
 *   COutputFilter --(MEDIATYPE_Interleaved)--> DV output filter (FireWire device)
 *
 * An async output queue of depth 10 is requested (COutputGraph base class
 * parameter) so that HandleFrame() returns quickly without waiting for the
 * DV device to accept each frame.  The device runs at a fixed 25 or 30 fps
 * and the queue absorbs transient timing jitter from the source files.
 *
 * The graph is started immediately after construction.  The run call is
 * made with a fallback: if Run() does not return S_OK it waits up to
 * 1000 ms for the graph to reach running state before throwing.
 *
 * vdst - Friendly name of the FireWire output device.
 * type - DV media type matching the frames that will be delivered.
 */
CDVOutput::CDVOutput(LPCSTR vdst, CMediaType *type)
: COutputGraph(type, 10)  /* queue depth 10: absorbs jitter without large latency */
{
	HRESULT hr;
	IBaseFilter *pVDST;
	CArray<CString,CString&> list;
	/* Enumerate and bind the named output device. */
	EnumVideoDevices(TRUE, vdst, list, &pVDST);
	CtrlAttach(pVDST);
	hr = m_FG->AddFilter(pVDST, L"DVout");
	CHECK_HR(hr);
	/* Connect: COutputFilter -> DV device filter. */
	hr = m_GB->RenderStream(NULL, NULL, (IBaseFilter*)m_outputFilter, NULL, pVDST);
	pVDST->Release();
	CHECK_HR(hr);
#ifdef DEBUG
	DumpGraph(m_FG, 0);
#endif
	/* Start the graph so the DV device is ready to receive frames immediately. */
	hr = m_MC->Run();
	if (hr != S_OK) {
		/* Run() returns S_FALSE when the graph is transitioning; wait for it. */
		OAFilterState state;
		hr = m_MC->GetState(1000, &state);
		CHECK_HR(hr, "Can't start DV output");
		if (state != State_Running)
			ThrowDShowException(CDShowException::error, "DV output not running");
	}
}

/*
 * CDVOutput destructor
 *
 * Sends EOS to flush the async queue and the DV device, then waits up to
 * 5 seconds for the graph to signal EC_COMPLETE before allowing the base
 * class to stop and release the filter graph.  This ensures the device
 * finishes writing all queued frames before the graph is torn down.
 */
CDVOutput::~CDVOutput()
{
	m_outputFilter->m_output->DeliverEndOfStream();
	if (m_ME) {
		long evCode;
		m_ME->WaitForCompletion(5000, &evCode);
	}
}

/////////////////////////////////////////////////////////////////////////////
// CAVIWriter
/////////////////////////////////////////////////////////////////////////////

/*
 * CAVIWriter constructor
 *
 * Builds the AVI capture graph and immediately starts recording.
 *
 * Temporary filename strategy:
 *   The AVI file is first opened under a "~"-prefixed temporary name
 *   (m_tmpfile) computed by GetCaptureFilename() with a "~"+dtformat
 *   prefix.  This prevents a partially-written AVI from being confused
 *   with a complete file if capture is interrupted.  The destructor renames
 *   the temp file to the final name (m_filename) after the graph completes.
 *
 * Type-2 AVI graph (type2AVI == true):
 *   COutputFilter -> DV Splitter -> AVI Mux (video + audio) -> File Sink
 *   Produces separate video and audio streams; more compatible with editing apps.
 *
 * Type-1 AVI graph (type2AVI == false):
 *   COutputFilter -> AVI Mux (single interleaved stream) -> File Sink
 *   Simpler topology; preserves the raw DV interleaved stream.
 *
 * AM_FILE_OVERWRITE is set on the file sink so that any pre-existing temp
 * file from an aborted previous session is silently overwritten.
 *
 * Parameters:
 *   filename  - Base output path (without extension).
 *   dtformat  - strftime date/time suffix format (may be empty).
 *   ndigits   - Minimum digit width for auto-increment numeric suffix.
 *   tim       - Seed time for the date/time suffix.
 *   type2AVI  - TRUE = Type-2 AVI (split), FALSE = Type-1 AVI (interleaved).
 *   type      - DV media type negotiated from the capture source.
 */
CAVIWriter::CAVIWriter(LPCSTR filename, LPCSTR dtformat, int ndigits, time_t tim, bool type2AVI, CMediaType *type)
: COutputGraph(type), m_filename(filename), m_dtformat(dtformat), m_ndigits(ndigits), m_dvtime(tim)
{
	HRESULT hr;
	IBaseFilter * pMux; IFileSinkFilter *pFile;
	WCHAR wbuf[256];
	/* Build temp filename: prepend "~" to the dtformat to distinguish temp files. */
	CString tmp = "~";
	m_tmpfile = GetCaptureFilename(m_filename, tmp+m_dtformat, m_ndigits, m_dvtime);
	MultiByteToWideChar(CP_ACP, 0, m_tmpfile, -1, wbuf, sizeof wbuf/ sizeof (WCHAR));
	/* Create the AVI mux and file sink via the capture graph builder helper. */
	m_GB->SetOutputFileName(&MEDIASUBTYPE_Avi, wbuf, &pMux, &pFile);
	/* Upgrade to IFileSinkFilter2 to set overwrite mode on the file. */
	IFileSinkFilter2 *pFile2;
	hr = pFile->QueryInterface(IID_IFileSinkFilter2, (void**)&pFile2);
	pFile->Release();
	if (SUCCEEDED(hr) && pFile2) {
		pFile2->SetMode(AM_FILE_OVERWRITE);
		pFile2->Release();
	}
	if (type2AVI) {
		/* Type-2: insert DV Splitter to demux video and audio into separate AVI streams. */
		IBaseFilter *pDVSplit;
		hr = CoCreateInstance((REFCLSID)CLSID_DVSplitter,
							  NULL, CLSCTX_INPROC, (REFIID)IID_IBaseFilter,
							  (void **)&pDVSplit);
		CHECK_HR(hr);
		hr = m_FG->AddFilter(pDVSplit, L"DV splitter");
		CHECK_HR(hr);
		/* COutputFilter -> DV Splitter -> AVI Mux (video) */
		hr = m_GB->RenderStream(NULL, &MEDIATYPE_Interleaved, (IBaseFilter*)m_outputFilter, NULL, pDVSplit);
		CHECK_HR(hr);
		hr = m_GB->RenderStream(NULL, &MEDIATYPE_Video, pDVSplit, NULL, pMux);
		CHECK_HR(hr);
		hr = m_GB->RenderStream(NULL, &MEDIATYPE_Audio, pDVSplit, NULL, pMux);
		CHECK_HR(hr);
		pDVSplit->Release();
	}
	else {
		/* Type-1: COutputFilter -> AVI Mux directly (single interleaved DV stream). */
		hr = m_GB->RenderStream(NULL, &MEDIATYPE_Interleaved, (IBaseFilter*)m_outputFilter, NULL, pMux);
		CHECK_HR(hr);
	}
	pMux->Release();
#ifdef DEBUG
	DumpGraph(m_FG, 0);
#endif
	/* Start the graph; frames delivered via HandleFrame() will be written immediately. */
	m_MC->Run();
}

/*
 * CAVIWriter destructor
 *
 * Sends EOS, waits up to 5 seconds for the AVI mux to flush and finalize
 * the file index, then renames the temp file to its final name.
 *
 * GetCaptureFilename() is called a second time here (without the "~" prefix)
 * to compute the final filename from the same parameters used at construction.
 * MoveFile() performs an atomic rename on the same volume.
 */
CAVIWriter::~CAVIWriter()
{
	/* Signal EOS so the AVI mux writes the file index and closes cleanly. */
	m_outputFilter->m_output->DeliverEndOfStream();
	if (m_ME) {
		long evCode;
		m_ME->WaitForCompletion(5000, &evCode);
	}
	if (m_MC) m_MC->Stop();
	/* Compute the final filename and atomically rename from temp.
	 * If MoveFile fails (permissions, disk full), the temp file is kept
	 * so the captured data is not lost. */
	CString tmp = GetCaptureFilename(m_filename, m_dtformat, m_ndigits, m_dvtime);
	if (!MoveFile(m_tmpfile, tmp)) {
		TRACE("CAVIWriter: MoveFile(\"%s\", \"%s\") failed, error %d\n",
			  (LPCSTR)m_tmpfile, (LPCSTR)tmp, GetLastError());
	}
}

/////////////////////////////////////////////////////////////////////////////
// CMonitor
/////////////////////////////////////////////////////////////////////////////

/* Static thread-proc trampoline for the monitoring thread. */
UINT MonitoringThread(LPVOID ptr)
{
	((CMonitor*)ptr)->MonitoringThread();
	return 0;
}

/*
 * CMonitor constructor
 *
 * Builds the preview rendering graph and starts both the DirectShow graph
 * and the MonitoringThread.
 *
 * Graph topology:
 *   COutputFilter -> (auto-connect to) DV Video Decoder -> Video Renderer
 *
 * RenderStream with NULL sink lets DirectShow automatically select and
 * connect a decoder and renderer.  SetDVDecoding() then downgrades to
 * half-D1 (360x240) for efficient preview rendering.
 *
 * The IVideoWindow interface is used to embed the renderer as a child of
 * m_hWnd (the CDV control window) with WS_CHILD style.
 */
CMonitor::CMonitor(HWND hWnd, CMediaType *type)
: COutputGraph(type), m_hWnd(hWnd), m_VW(NULL), m_sample(NULL)
{
	HRESULT hr;
	hr = m_FG->QueryInterface(IID_IVideoWindow, (void **)&m_VW);
	CHECK_HR(hr);
	/* Build the decode + render chain automatically from COutputFilter. */
	hr = m_GB->RenderStream(NULL, NULL, (IBaseFilter*)m_outputFilter, NULL, NULL);
	CHECK_HR(hr, "Can't build preview render chain");
	/* Reduce decoder resolution to half-D1 for preview; saves CPU. */
	SetDVDecoding(m_FG, 0);
	/* Embed the video renderer window inside the CDV control. */
	hr = m_VW->put_Owner((LONG)m_hWnd);
	CHECK_HR(hr, "Can't set preview window owner");
	hr = m_VW->put_WindowStyle(WS_CHILD);
	CHECK_HR(hr, "Can't set preview window style");
	Resize();
	hr = m_MC->Run();
	CHECK_HR(hr, "Can't start preview graph");

	/* Start the monitoring thread suspended so we can clear m_bAutoDelete first. */
	m_thread = AfxBeginThread(::MonitoringThread,this,THREAD_PRIORITY_BELOW_NORMAL,0,CREATE_SUSPENDED);
	m_thread->m_bAutoDelete = FALSE;
	m_thread->ResumeThread();
}

/*
 * CMonitor destructor
 *
 * Releases IVideoWindow first (which removes the renderer from m_hWnd),
 * then signals m_ev to unblock MonitoringThread and waits for it to exit.
 * Ordering is critical: nulling m_VW causes MonitoringThread's !m_VW check
 * to trigger a clean return from its loop.
 */
CMonitor::~CMonitor()
{
	if (m_VW) m_VW->Release();
	m_VW = NULL;
	/* Wake MonitoringThread so it can observe m_VW == NULL and exit. */
	m_ev.SetEvent();

	WaitForSingleObject(m_thread->m_hThread, INFINITE);
	delete m_thread;
}

/*
 * CMonitor::Resize
 *
 * Repositions the IVideoWindow inside the parent control, maintaining
 * DV's 4:3 aspect ratio and centring the letterboxed image.
 *
 * The window is sized to the largest 4:3 rectangle that fits within the
 * control's client area, then centred by computing the offset as
 * (parent_dim - scaled_dim) / 2.
 */
void CMonitor::Resize() {
	ULONG cx, cy, w, h;
	RECT rect;
	GetClientRect(m_hWnd, &rect);
	cx = rect.right - rect.left;
	cy = rect.bottom - rect.top;

	/* Scale from both axes and take the smaller to fit inside the control. */
	w = cy*4/3;//*m_width/m_height;
	h = cx*3/4;//*m_height/m_width;
	if (cx < w) w = cx;
	if (cy < h) h = cy;
	/* Centre the scaled window within the control. */
	m_VW->SetWindowPosition((cx-w)/2, (cy-h)/2, w, h);
}

/*
 * CMonitor::HandleFrame
 *
 * Called on the DirectShow streaming thread (or CapturingThread) with each
 * incoming DV frame.
 *
 * If MonitoringThread has already allocated a buffer (m_sample != NULL),
 * the frame data is copied in, the pointer is cleared, and m_ev is signaled
 * so MonitoringThread delivers the sample.
 *
 * If m_sample is NULL (MonitoringThread has not yet requested a buffer, or
 * the preview is falling behind), the frame is silently dropped.  This is
 * intentional: preview should never block or slow down the capture/record path.
 */
void CMonitor::HandleFrame(REFERENCE_TIME duration, BYTE *data, int len)
{
	if (data) {
		if (m_sample) {
			BYTE *ptr;
			HRESULT hr = m_sample->GetPointer(&ptr);
			if (FAILED(hr) || !ptr) {
				m_sample = NULL;
				return;
			}
			CopyMemory(ptr, data, len);
			m_sample->SetActualDataLength(len);
			m_sample->SetSyncPoint(true);
			/* Clear m_sample before signaling so MonitoringThread can safely deliver. */
			m_sample = NULL;
			m_ev.SetEvent();
		}
	}
}

/*
 * CMonitor::MonitoringThread
 *
 * Rate-limited preview delivery loop running at THREAD_PRIORITY_BELOW_NORMAL.
 *
 * Algorithm:
 *   1. Acquire an IMediaSample allocator buffer (blocks if all buffers are in use).
 *   2. Compute how long since the last frame was delivered; sleep the remainder
 *      to maintain roughly 5 fps (200 ms per frame).  The dynamic sleep avoids
 *      busy-waiting while remaining responsive to the actual delivery rate.
 *   3. Expose the buffer via m_sample so the next HandleFrame() call will fill it.
 *   4. Wait on m_ev until HandleFrame() signals that data is ready (or CMonitor
 *      destructor sets m_VW = NULL to signal shutdown).
 *   5. If m_VW is NULL, release the buffer and exit (destructor is running).
 *   6. Deliver the filled sample to the downstream renderer.
 *
 * The 200 ms maximum sleep caps preview at ~5 fps regardless of input rate,
 * saving decode and render CPU for the capture/record path.
 */
void CMonitor::MonitoringThread()
{
	DWORD ticks = 0;
	for(;;) {
		IMediaSample *pSample = NULL;
		HRESULT hr;
		/* Block until an allocator buffer is available from the downstream renderer. */
		hr = m_outputFilter->m_output->GetDeliveryBuffer(&pSample, NULL, NULL, 0);
		if (hr == NOERROR) {
			/* Dynamic throttle: sleep the time elapsed since last delivery,
			 * capped at 200 ms, to target ~5 fps maximum preview rate. */
			ticks = GetTickCount() - ticks;
			Sleep(ticks < 200 ? ticks + 10 : 200);
			/* Expose the buffer to HandleFrame() so it can fill it with the latest frame. */
			m_sample = pSample;
			CSingleLock lck(&m_ev);
			lck.Lock();  /* wait for HandleFrame() to fill m_sample and signal m_ev */
			if (!m_VW) {
				/* Destructor has set m_VW = NULL; release and exit cleanly. */
				pSample->Release();
				return;
			}
			ticks = GetTickCount();
			/* Deliver the filled sample to the renderer. */
			m_outputFilter->m_output->Deliver(pSample);
			pSample->Release();
			pSample = NULL;
		}
		else {
			/* GetDeliveryBuffer failed (graph stopping); back off before retrying. */
			Sleep(100);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////
// CDVQueue  (thread-safe ring buffer)
/////////////////////////////////////////////////////////////////////////////

/*
 * CDVQueue constructor
 *
 * Allocates the ring buffer as a single contiguous block of memory, then
 * initialises m_queue[] as an array of pointers into that block.
 *
 * The ring uses queueSize+1 slots internally: one slot is always kept free
 * to let m_load unambiguously represent the number of filled slots (avoids
 * the head==tail ambiguity of a classic two-pointer ring).
 *
 * Memory layout of m_buffers:
 *   [ Buffer₀ | data₀[dataSize] | Buffer₁ | data₁[dataSize] | ... ]
 * where sizeof(Buffer) includes the one-byte flexible array member (data[1]).
 *
 * queueSize - Maximum frames in flight simultaneously (100 in practice).
 * dataSize  - Maximum bytes per frame (typically 144000 for PAL).
 */
CDVQueue::CDVQueue(int queueSize, int dataSize)
: m_buffers(NULL), m_queue(NULL), m_dataSize(dataSize), m_queueSize(queueSize+1),
  m_head(0), m_tail(0), m_load(0), m_end(false)
{
	/* Allocate all ring slot memory in one block for cache locality. */
	m_buffers = new BYTE[(sizeof (Buffer) + m_dataSize) * m_queueSize];
	m_queue = new Buffer *[m_queueSize];
	/* Point each m_queue[i] to the start of its slot in m_buffers. */
	for(int i=0; i<m_queueSize; i++) {
		m_queue[i] = (Buffer *) (m_buffers + (sizeof (Buffer) + m_dataSize) * i);
	}
}

CDVQueue::~CDVQueue()
{
	delete[] m_buffers;
	delete[] m_queue;
}

/*
 * CDVQueue::Put  (producer side)
 *
 * Enqueues one DV frame into the ring.  If the ring is full, waits on
 * m_evPut (signaled by Get() when it frees a slot) before retrying.
 *
 * End-of-stream (data == NULL):
 *   Sets m_end = true and signals both m_evGet and m_evPut so that:
 *   - A blocked Get() consumer wakes and returns false.
 *   - A blocked Put() producer (if any) can also exit its wait loop.
 *
 * Thread safety: m_cs protects m_tail and m_load for the write path.
 */
void CDVQueue::Put(REFERENCE_TIME duration, BYTE *data, int len)
{
	if (!data) {
		/* End-of-stream sentinel: signal both events and set the flag. */
		m_end = true;
		m_evGet.SetEvent();
		m_evPut.SetEvent();
		return;
	}
	while(!m_end) {
		{
			CAutoLock lock(&m_cs);
			if (m_load < m_queueSize-1) {
				/* Write the frame into the tail slot and advance the tail.
			 * Clamp len to m_dataSize to prevent buffer overflow. */
				if (len > m_dataSize) len = m_dataSize;
				m_queue[m_tail]->duration = duration;
				m_queue[m_tail]->len = len;
				CopyMemory(m_queue[m_tail]->data, data, len);
				m_tail = (m_tail+1) % m_queueSize;
				m_load++;
				/* Wake consumer if it was blocked waiting for data. */
				m_evGet.SetEvent();
				break;
			}
		}
		/* Ring is full; wait for the consumer to free a slot. */
		CSingleLock lck(&m_evPut);
		lck.Lock();
	}
}


/*
 * CDVQueue::Get  (consumer side)
 *
 * Dequeues the oldest frame from the ring.  If the ring is empty, waits on
 * m_evGet (signaled by Put() when it fills a slot) before retrying.
 *
 * Returns false when m_end is set AND the ring is empty, indicating that the
 * producer has finished and all frames have been consumed.  The caller
 * (CapturingThread / RecordingThread) treats false as end-of-stream.
 *
 * IMPORTANT: *data points directly into the ring slot.  The caller must
 * consume or copy the data before calling Get() again, as the next Put()
 * may overwrite the same slot.
 *
 * Thread safety: m_cs protects m_head and m_load for the read path.
 */
bool CDVQueue::Get(REFERENCE_TIME *duration, BYTE **data, int *len)
{
	for(;;) {
		{
			CAutoLock lock(&m_cs);
			if (m_load > 0) {
				/* Read from the head slot and advance the head pointer. */
				*duration = m_queue[m_head]->duration;
				*len = m_queue[m_head]->len;
				*data = m_queue[m_head]->data;
				m_head = (m_head+1) % m_queueSize;
				m_load--;
				/* Wake producer if it was blocked waiting for space. */
				m_evPut.SetEvent();
				break;
			}
		}
		/* Ring is empty; if end-of-stream was signaled, report completion. */
		if (m_end) return false;
		/* Wait for the producer to enqueue another frame. */
		CSingleLock lck(&m_evGet);
		lck.Lock();
	}
	return true;
}

/*
 * CDVQueue::GetWithTimeout  (consumer side, timeout variant)
 *
 * Same as Get(), but waits at most timeoutMs milliseconds for a frame
 * to become available.  Returns false on timeout OR end-of-stream.
 * Used by CapturingThread for end-of-signal auto-detection: if the DV
 * device stops sending frames (tape end, disconnect), this returns
 * false after the timeout instead of blocking indefinitely.
 */
bool CDVQueue::GetWithTimeout(REFERENCE_TIME *duration, BYTE **data, int *len, DWORD timeoutMs)
{
	for(;;) {
		{
			CAutoLock lock(&m_cs);
			if (m_load > 0) {
				*duration = m_queue[m_head]->duration;
				*len = m_queue[m_head]->len;
				*data = m_queue[m_head]->data;
				m_head = (m_head+1) % m_queueSize;
				m_load--;
				m_evPut.SetEvent();
				break;
			}
		}
		if (m_end) return false;
		/* Wait with timeout instead of blocking indefinitely. */
		if (WaitForSingleObject(m_evGet.m_hObject, timeoutMs) == WAIT_TIMEOUT) {
			return false;
		}
	}
	return true;
}

/////////////////////////////////////////////////////////////////////////////
// SHA-256 helpers (WDV-11)
/////////////////////////////////////////////////////////////////////////////

/*
 * ComputeFileSHA256 — read a file in 64 KB blocks and compute its SHA-256.
 *
 * On success, writes the 64-char lowercase hex hash to szHashOut and returns TRUE.
 * On failure (file not found, read error), sets szHashOut[0] = '\0' and returns FALSE.
 */
BOOL ComputeFileSHA256(LPCSTR szFilePath, char szHashOut[65])
{
	szHashOut[0] = '\0';

	HANDLE hFile = CreateFile(szFilePath, GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
		return FALSE;

	SHA256_CTX ctx;
	sha256_init(&ctx);

	BYTE buf[65536];
	DWORD dwRead;
	while (ReadFile(hFile, buf, sizeof buf, &dwRead, NULL) && dwRead > 0) {
		sha256_update(&ctx, buf, dwRead);
	}

	CloseHandle(hFile);

	unsigned char hash[32];
	sha256_final(&ctx, hash);
	sha256_hex(hash, szHashOut);

	return TRUE;
}

/*
 * WriteSHA256Sidecar — write a sha256sum-compatible sidecar file.
 *
 * Format: "<64-hex> *<filename>\n"
 * bAppend=TRUE appends a line (for scene-split mode).
 * bAppend=FALSE overwrites (single file capture).
 */
void WriteSHA256Sidecar(LPCSTR szSidecarPath, LPCSTR szHash, LPCSTR szFilename, BOOL bAppend)
{
	FILE *f = fopen(szSidecarPath, bAppend ? "a" : "w");
	if (!f) return;
	fprintf(f, "%s *%s\n", szHash, szFilename);
	fclose(f);
}

/*
 * VerifyFileSHA256 — verify a file against its .sha256 sidecar.
 *
 * Returns: 0 = OK, 1 = hash mismatch, 2 = sidecar not found or unreadable.
 */
int VerifyFileSHA256(LPCSTR szFilePath)
{
	CString sidecarPath;
	sidecarPath.Format("%s.sha256", szFilePath);

	FILE *f = fopen(sidecarPath, "r");
	if (!f) return 2;

	/* Extract bare filename from path for matching. */
	CString bareName = szFilePath;
	int sep = bareName.ReverseFind('\\');
	if (sep >= 0) bareName = bareName.Mid(sep + 1);

	/* Scan sidecar lines for a matching filename. */
	char line[512];
	char expectedHash[65] = "";
	while (fgets(line, sizeof line, f)) {
		/* Format: "<64hex> *<filename>\n" or "<64hex>  <filename>\n" */
		if (strlen(line) < 66) continue;
		char *star = strchr(line + 64, '*');
		if (!star) star = strchr(line + 64, ' ');
		if (!star) continue;
		star++;
		while (*star == ' ') star++;
		/* Strip trailing newline/CR. */
		char *end = star + strlen(star) - 1;
		while (end >= star && (*end == '\n' || *end == '\r')) *end-- = '\0';
		if (_stricmp(star, bareName) == 0) {
			memcpy(expectedHash, line, 64);
			expectedHash[64] = '\0';
			break;
		}
	}
	fclose(f);

	if (expectedHash[0] == '\0') return 2;

	char actualHash[65];
	if (!ComputeFileSHA256(szFilePath, actualHash))
		return 2;

	return (_stricmp(expectedHash, actualHash) == 0) ? 0 : 1;
}

/////////////////////////////////////////////////////////////////////////////
// Capture Log
/////////////////////////////////////////////////////////////////////////////

/*
 * WriteCaptureLog — append one CSV row summarizing a capture session.
 *
 * Creates the file with a header row if it does not exist yet.
 * Silently returns if the file cannot be opened (e.g. read-only dir).
 */
void WriteCaptureLog(LPCSTR szLogPath, const CaptureStats& stats)
{
	static const char *stopReasons[] = {"USER", "SIGNAL_LOST", "LOW_DISK", "TIMED"};

	BOOL isNew = (GetFileAttributes(szLogPath) == INVALID_FILE_ATTRIBUTES);
	FILE *f = fopen(szLogPath, "a");
	if (!f) return;

	if (isNew) {
		fprintf(f, "Timestamp,Filename,Format,Frames,Dropped,TapeStart,TapeEnd,Duration_s,StopReason,AVI_OK,Frames_Defekt,Index_Vorhanden,Fehler_Frames,Fehler_Bloecke_Video,Fehler_Bloecke_Audio,Fehler_Prozent,Schlimmster_Frame,Even_Odd_Delta,SHA256\n");
	}

	/* Wall-clock timestamp for the log entry. */
	time_t now = time(NULL);
	char szNow[32];
	strftime(szNow, sizeof szNow, "%Y-%m-%dT%H:%M:%S", localtime(&now));

	/* Format tape timestamps (empty if not available). */
	char szTapeStart[32] = "", szTapeEnd[32] = "";
	if (stats.tFirstRecTime > 0)
		strftime(szTapeStart, sizeof szTapeStart, "%Y-%m-%dT%H:%M:%S", localtime(&stats.tFirstRecTime));
	if (stats.tLastRecTime > 0)
		strftime(szTapeEnd, sizeof szTapeEnd, "%Y-%m-%dT%H:%M:%S", localtime(&stats.tLastRecTime));

	DWORD durationSec = (GetTickCount() - stats.dwStartTick) / 1000;
	const char *reason = (stats.eStopReason >= 0 && stats.eStopReason <= 3)
		? stopReasons[stats.eStopReason] : "UNKNOWN";

	/* WDV-10: Compute error percentage and even/odd delta. */
	double errorPct = 0.0;
	if (stats.errorStats.dwTotalFrames > 0)
		errorPct = 100.0 * stats.errorStats.dwFramesWithVideoErrors / stats.errorStats.dwTotalFrames;
	long evenOddDelta = (long)stats.errorStats.dwVideoErrorsEven - (long)stats.errorStats.dwVideoErrorsOdd;
	if (evenOddDelta < 0) evenOddDelta = -evenOddDelta;

	fprintf(f, "%s,%s,%s,%lu,%lu,%s,%s,%lu,%s,%d,%lu,%d,%lu,%lu,%lu,%.2f,%lu,%ld,%s\n",
		szNow,
		(LPCSTR)stats.sFilename,
		stats.bIsPAL ? "PAL" : "NTSC",
		stats.dwFrameCount,
		stats.dwDroppedFrames,
		szTapeStart,
		szTapeEnd,
		durationSec,
		reason,
		stats.bCheckPassed ? 1 : 0,
		stats.dwCheckDefect,
		stats.bCheckIndex ? 1 : 0,
		stats.errorStats.dwFramesWithVideoErrors,
		stats.errorStats.dwTotalVideoErrorBlocks,
		stats.errorStats.dwTotalAudioErrorBlocks,
		errorPct,
		stats.errorStats.dwWorstFrameNumber,
		evenOddDelta,
		stats.szSHA256);

	fclose(f);
}

/////////////////////////////////////////////////////////////////////////////
// CDV — top-level orchestrator
/////////////////////////////////////////////////////////////////////////////

/* Static thread-proc trampolines; required because AfxBeginThread takes a plain function pointer. */
static UINT RecordingThread(LPVOID pdv)
{
	((CDV *)pdv)->RecordingThread();
	return 0;
}
static UINT CapturingThread(LPVOID pdv)
{
	((CDV *)pdv)->CapturingThread();
	return 0;
}

/*
 * CDV constructor
 *
 * Initialises all pipeline pointers to NULL and sets default parameters.
 * No DirectShow objects are created here; BuildCapturing() / BuildRecording()
 * do that on demand.
 *
 * Default values:
 *   m_type2AVI            = true  (Type-2 AVI, separate audio/video streams)
 *   m_discontinuityTreshold = 1   (any DV timestamp jump triggers a new file)
 *   m_maxAVIFrames        = 25*60*15 (split at 15 minutes for NTSC/PAL)
 *   m_everyNth            = 1    (capture every frame)
 *   m_recordPreview       = TRUE (show preview during recording)
 *   m_DVctrl              = FALSE (no automatic tape transport control)
 */
CDV::CDV()
: m_aviJoiner(NULL), m_aviWriter(NULL), m_dvInput(NULL), m_dvOutput(NULL), m_monitor(NULL), m_queue(NULL),
  m_thread(NULL),
  m_type2AVI(true), m_discontinuityTreshold(1), m_maxAVIFrames(25*60*15), m_everyNth(1), m_recordPreview(TRUE),
  m_dropped(0), m_counter(-1), m_time(-1), m_captureTime(0), m_ndigits(0), m_DVctrl(FALSE),
  m_autoStopTimeout(5000),
  m_enableSHA256(true)
{
	ResetErrorStats(&m_errorStats);
}

CDV::~CDV()
{
	Destroy();
}

/* Propagates resize events to the preview monitor to maintain the correct aspect ratio. */
void CDV::OnSize(UINT nType, int cx, int cy)
{
	CStatic::OnSize(nType, cx, cy);

	if (m_monitor) m_monitor->Resize();
}

/* Returns the current pipeline state (one of the Idle/Capturing/... enum values). */
int CDV::GetState()
{
	return m_state;
}

/* Returns the accumulated dropped-frame count for the current capture session. */
int CDV::GetDropped()
{
	return m_dropped;
}

/* Returns the total frames written to disk in the current session. */
long CDV::GetCounter()
{
	return m_counter;
}

/* Returns the total recorded duration in 100-ns REFERENCE_TIME units. */
REFERENCE_TIME CDV::GetTime()
{
	return m_time;
}

/* Returns the current queue fill level (number of frames buffered). */
int CDV::GetQueueLoad()
{
	return m_queue->m_load;
}

/* WDV-10: Returns a snapshot of the current DV error statistics.
 * Copies under m_cs lock for safe access from the UI timer thread. */
ErrorStats CDV::GetErrorStats()
{
	CAutoLock lock(&m_cs);
	return m_errorStats;
}

/* Returns the filename of the currently open AVI file (thread-safe via m_cs). */
CString CDV::GetCaptureFilename()
{
	CAutoLock lock(&m_cs);

	return m_captureFilename;
}

/*
 * CDV::Destroy
 *
 * Tears down the complete pipeline and resets all state to Idle.
 *
 * Shutdown sequence:
 *   1. Set m_state = Idle so CapturingThread / RecordingThread exit their loops.
 *   2. Signal CDVQueue end-of-stream (Put(NULL)) to unblock any blocked Get() call.
 *   3. Stop the input source (DVInput or AVIJoiner) to stop frame production.
 *   4. Wait for the worker thread to exit, then delete it.
 *   5. Delete all pipeline objects (which finalise AVI files, wait for EOS, etc.).
 *   6. Reset all counters.
 *
 * This method is safe to call in any state, including Idle.
 */
void CDV::Destroy()
{
	m_state = Idle;

	/* Signal the queue end-of-stream so CapturingThread/RecordingThread can exit. */
	if (m_queue) {m_queue->Put(-1, NULL, 0);}

	/* Stop the frame source so no new frames enter the queue. */
	if (m_aviJoiner) m_aviJoiner->Stop();
	if (m_dvInput) m_dvInput->Stop();

	/* Wait for the worker thread to finish processing remaining queued frames. */
	if (m_thread) {
		WaitForSingleObject(m_thread->m_hThread, INFINITE);
		delete m_thread;
		m_thread = NULL;
	}

	/* Tear down pipeline objects in reverse construction order.
	 * Destructor ordering matters: CAVIWriter/CDVOutput wait for EOS before returning. */
	if (m_aviJoiner) { delete(m_aviJoiner);	m_aviJoiner = NULL; }
	if (m_dvInput) {
		if (m_DVctrl) m_dvInput->CtrlStop();
		delete(m_dvInput);	m_dvInput = NULL;
	}
	if (m_aviWriter) { delete(m_aviWriter);	m_aviWriter = NULL; }
	if (m_dvOutput) {
		if (m_DVctrl) m_dvOutput->CtrlStop();
		delete(m_dvOutput); m_dvOutput = NULL;
	}
	if (m_monitor) { delete(m_monitor);	m_monitor = NULL; }
	if (m_queue) { delete(m_queue);	m_queue = NULL; }
	/* Reset session counters. */
	m_dropped = 0;
	m_counter = -1;
	m_time = -1;
	m_captureTime = 0;
}


/*
 * CDV::HandleFrame  (CFrameHandler implementation)
 *
 * Called on the DirectShow streaming thread once per incoming DV frame.
 * Simply enqueues the frame into CDVQueue; the real work happens in
 * CapturingThread / RecordingThread on the worker thread.
 *
 * This method must be fast and non-blocking: CDVQueue::Put() will block
 * only if the ring is full, providing the necessary backpressure.
 */
void CDV::HandleFrame(REFERENCE_TIME duration, BYTE *data, int len)
{
	m_queue->Put(duration, data, len);
}


/*
 * CDV::BuildCapturing
 *
 * Constructs the capture pipeline for DV-to-AVI recording and enters
 * CapturePaused state.  No frames are written to disk until StartCapturing()
 * is called.
 *
 * Pipeline constructed:
 *   CDVInput (FireWire) --> CDV::HandleFrame --> CDVQueue
 *   CMonitor (preview)  <-- CapturingThread  <--
 *
 * The CapturingThread is started immediately (suspended then resumed) so
 * it is ready to begin writing the moment StartCapturing() changes state
 * to Capturing.
 *
 * vsrc - Friendly name of the FireWire capture device.
 */
void CDV::BuildCapturing(LPCSTR vsrc)
{
	Destroy();
	HRESULT hr = S_OK;

	m_dvInput = new CDVInput(vsrc);

	/* Determine the actual DV format (NTSC/PAL, sample size) from the live source. */
	CMediaType type;
	m_dvInput->GetMediaType(&type);

	m_monitor = new CMonitor(m_hWnd, &type);

	/* Queue of 100 frames provides ~4 seconds of buffer at 25 fps (PAL). */
	m_queue = new CDVQueue(100, type.GetSampleSize());

	/* Start the capture graph; frames arrive via CDV::HandleFrame. */
	m_dvInput->Run(this);

	m_state = CapturePaused;

	/* Start the worker thread suspended; resume after clearing m_bAutoDelete
	 * so we can safely wait for it in Destroy(). */
	m_thread = AfxBeginThread(::CapturingThread,this,THREAD_PRIORITY_NORMAL,0,CREATE_SUSPENDED);
	m_thread->m_bAutoDelete = FALSE;
	m_thread->ResumeThread();

	InvalidateRect(NULL);
	UpdateWindow();
}

/* Pauses an active capture by transitioning to CapturePaused.
 * The current AVI file is closed by CapturingThread on the next frame. */
void CDV::StopCapturing()
{
	if (m_state == Capturing) {
		m_state = CapturePaused;
		if (m_DVctrl) m_dvInput->CtrlPause();
	}
}

/*
 * CDV::StartCapturing
 *
 * Transitions from CapturePaused to Capturing.  CapturingThread will open
 * a new CAVIWriter on the next frame after observing the state change.
 *
 * captureTime > 0 enables automatic stop after the specified duration
 * (in 100-ns REFERENCE_TIME units).  The thread sets m_state = Finished
 * when m_time >= m_captureTime.
 */
void CDV::StartCapturing(LPCSTR filename, LPCSTR dtformat, int ndigits, REFERENCE_TIME captureTime)
{
	if (m_state == CapturePaused) {
		m_captureTime = captureTime;
		m_captureFilename = filename;
		m_dtformat = dtformat;
		m_ndigits = ndigits;
		m_state = Capturing;
		if (m_DVctrl) m_dvInput->CtrlPlay();
	}
}


/*
 * CDV::BuildRecording
 *
 * Constructs the playback-to-DV recording pipeline and enters RecordPaused
 * state.  No frames are sent to the DV device until StartRecording() is called.
 *
 * Pipeline constructed:
 *   CAVIJoiner (AVI files) --> CDV::HandleFrame --> CDVQueue
 *   CMonitor (preview)     <-- RecordingThread  -->  CDVOutput (FireWire)
 *
 * If m_DVctrl is set, the DV device is placed into record-pause (standby)
 * so that StartRecording() can engage record mode immediately with minimal
 * latency.
 *
 * filenames - Pipe-separated list of AVI paths / glob patterns.
 * vdst      - Friendly name of the FireWire output device.
 */
void CDV::BuildRecording(LPCSTR filenames, LPCSTR vdst)
{
	Destroy();

	m_aviJoiner = new CAVIJoiner(filenames);

	CMediaType type;
	m_aviJoiner->GetMediaType(&type);

	m_monitor = new CMonitor(m_hWnd, &type);
	m_dvOutput = new CDVOutput(vdst, &type);

	m_queue = new CDVQueue(100, type.GetSampleSize());

	/* Start the joiner; frames flow into CDVQueue via CDV::HandleFrame. */
	m_aviJoiner->Run(this);

	m_state = RecordPaused;

	/* Pre-arm the DV device in record-pause so it's ready for StartRecording(). */
	if (m_DVctrl) m_dvOutput->CtrlRecPause();

	m_thread = AfxBeginThread(::RecordingThread,this,THREAD_PRIORITY_NORMAL,0,CREATE_SUSPENDED);
	m_thread->m_bAutoDelete = FALSE;
	m_thread->ResumeThread();

	InvalidateRect(NULL);
	UpdateWindow();
}

/* Pauses recording; no frames are forwarded to CDVOutput while in RecordPaused. */
void CDV::StopRecording()
{
	if (m_state == Recording) {
		m_state = RecordPaused;
		if (m_DVctrl) m_dvOutput->CtrlRecPause();
	}
}

/* Transitions from RecordPaused to Recording; RecordingThread begins forwarding frames. */
void CDV::StartRecording()
{
	if (m_state == RecordPaused) {
		m_state = Recording;
		if (m_DVctrl) m_dvOutput->CtrlRecord();
	}
}

/*
 * CDV::CapturingThread
 *
 * Worker thread that consumes frames from CDVQueue and writes them to disk.
 *
 * Main responsibilities:
 *   1. Parse DV recording timestamps from each frame via GetDVRecordingTime()
 *      and post WM_DV_TIMECHANGE to the parent window for UI display.
 *   2. Throttle preview: only call CMonitor::HandleFrame() when the queue is
 *      less than half full.  When the disk is under pressure the queue fills
 *      up; skipping preview frames prevents the renderer from competing with
 *      disk I/O for CPU time.
 *   3. AVI file splitting:
 *      a. Frame count limit: if the current file has reached m_maxAVIFrames,
 *         close it and open a new CAVIWriter.
 *      b. Timestamp discontinuity: if |dvTime - oldDVTime| > m_discontinuityTreshold,
 *         a tape cut or camera clock reset has been detected; split to a new file.
 *      c. New file on resume: if m_aviWriter is NULL at the start of a Capturing
 *         period (first frame or after a split), a fresh CAVIWriter is created.
 *   4. Frame decimation: only every m_everyNth frame is written, controlled by
 *      a per-session counter.  All frames are still consumed from the queue to
 *      prevent backpressure from building up.
 *   5. DV timestamp backfill: if m_aviWriter->m_dvtime is 0 (the first frame
 *      had no valid timestamp), it is updated on the first frame that does have
 *      a valid timestamp.  This affects the filename generated at file close.
 *   6. Timed capture: if m_captureTime > 0 and accumulated m_time >= m_captureTime,
 *      the state transitions to Finished, which exits the loop on the next iteration.
 *   7. Pause handling: while m_state == CapturePaused the current CAVIWriter is
 *      closed.  Dropped-frame counters are reset so resumed capture starts fresh.
 *
 * Loop exit conditions:
 *   - CDVQueue::Get() returns false (EOS from the source).
 *   - m_state transitions to Idle (Destroy() was called).
 *   - m_state transitions to Finished (timed capture completed).
 */
void CDV::CapturingThread()
{
	BYTE *buffer;
	REFERENCE_TIME duration;
	int len;
	CMediaType type;
	m_dvInput->GetMediaType(&type);
	long nFrames = 0, counter = 0;
	/* Snapshot the driver's dropped-frame count at start; delta gives our count. */
	long dropped = m_dvInput->GetDroppedFrames();
	int dvTime = 0, oldDVTime = 0;
	long diskCheckCounter = 0;
	/* Capture statistics for the CSV log. */
	CaptureStats capStats;
	capStats.dwStartTick = GetTickCount();
	capStats.tFirstRecTime = 0;
	capStats.tLastRecTime = 0;
	capStats.dwFrameCount = 0;
	capStats.dwDroppedFrames = 0;
	capStats.bIsPAL = (type.GetSampleSize() >= 144000);
	capStats.eStopReason = 0;
	capStats.bCheckPassed = FALSE;
	capStats.dwCheckDefect = 0;
	capStats.bCheckIndex = FALSE;
	ResetErrorStats(&capStats.errorStats);
	capStats.szSHA256[0] = '\0';
	/* WDV-10: Reset cumulative error stats for this session. */
	ResetErrorStats(&m_errorStats);
	m_counter = 0;
	m_time = 0;
	/* m_state is checked at the top of the loop; Idle (0) exits immediately. */
	while (m_state) {
		/* Use timeout-based Get when auto-stop is enabled (m_autoStopTimeout > 0).
		 * If no frame arrives within the timeout, assume end of signal. */
		bool gotFrame;
		if (m_autoStopTimeout > 0)
			gotFrame = m_queue->GetWithTimeout(&duration, &buffer, &len, m_autoStopTimeout);
		else
			gotFrame = m_queue->Get(&duration, &buffer, &len);
		if (!gotFrame && !m_queue->m_end && m_state == Capturing) {
			/* Timeout with no EOS: signal lost (tape end or device disconnect). */
			capStats.eStopReason = 1; /* SIGNAL_LOST */
			GetParent()->PostMessage(WM_DV_SIGNALLOST, 0, 0);
			m_state = Finished;
			break;
		}
		if (gotFrame) {
			/* Extract the camcorder recording timestamp from the DV SSYB subcode. */
			long newDVTime = GetDVRecordingTime(buffer, len);
			int deltaDVTime = 0;
			if (dvTime != newDVTime) {
				if (newDVTime > 0) {
					if (oldDVTime > 0) {
						/* Compute absolute delta to detect tape cuts (sign-independent). */
						deltaDVTime = newDVTime - oldDVTime;
						if (deltaDVTime < 0) deltaDVTime = -deltaDVTime;
					}
					oldDVTime = newDVTime;
				}
				dvTime = newDVTime;
				/* Track first and last valid tape timestamps for the log. */
				if (newDVTime > 0) {
					if (capStats.tFirstRecTime == 0) capStats.tFirstRecTime = newDVTime;
					capStats.tLastRecTime = newDVTime;
				}
				/* Notify the UI of the new recording timestamp. */
				GetParent()->PostMessage(WM_DV_TIMECHANGE, 0, dvTime);
			}
			/* WDV-10: Analyze the DV frame for error concealment flags (STA). */
			FrameErrorInfo frameErrors = AnalyzeDVFrame(buffer, len);
			{
				CAutoLock lock(&m_cs);
				AccumulateErrorStats(&m_errorStats, &frameErrors, m_errorStats.dwTotalFrames);
			}

			/* Only send frames to the preview if the queue is below the halfway mark;
			 * when the queue fills up, skip preview to prioritise disk writes. */
			if (m_queue->m_load < m_queue->m_queueSize/2)
				m_monitor->HandleFrame(duration, buffer, len);

			if (m_state == Capturing) {
				/* --- File splitting logic ---
				 * Close and reopen the writer if:
				 *   (a) the current file has hit the frame count limit, or
				 *   (b) a DV timestamp discontinuity (tape cut) was detected. */
				if (m_aviWriter && (nFrames >= m_maxAVIFrames || ((m_discontinuityTreshold > 0) && (deltaDVTime > m_discontinuityTreshold)))) {
					delete m_aviWriter;
					m_aviWriter = NULL;
				}
				/* Open a new AVI file if none is currently open. */
				if (!m_aviWriter) {
					m_aviWriter = new CAVIWriter(m_captureFilename, m_dtformat, m_ndigits, dvTime, m_type2AVI, &type);
					nFrames = 0;
					counter = 0;
				}
				/* Frame decimation: only write every m_everyNth frame. */
				if ((counter % m_everyNth) == 0) {
					m_aviWriter->HandleFrame(duration, buffer, len);
					nFrames++;
				}
				/* Backfill the DV timestamp if the first frame(s) had no valid time. */
				if (dvTime && !m_aviWriter->m_dvtime) {
					m_aviWriter->m_dvtime = dvTime;
				}
				counter++;
				m_counter++;
				m_time += duration;
				/* Check whether the optional timed capture limit has been reached. */
				if (m_captureTime && (m_time >= m_captureTime)) {
					m_captureTime = 0;
					capStats.eStopReason = 3; /* TIMED */
					m_state = Finished;
				}
				/* Check free disk space approximately once per minute (~1500 frames).
				 * If below 500 MB, notify the UI so the user can act before data loss. */
				diskCheckCounter++;
				if (diskCheckCounter >= 1500) {
					diskCheckCounter = 0;
					ULARGE_INTEGER freeBytes;
					/* Extract drive root from the capture filename for the space query. */
					CString drivePath = m_captureFilename.Left(3);
					if (GetDiskFreeSpaceEx(drivePath, &freeBytes, NULL, NULL)) {
						DWORD freeMB = (DWORD)(freeBytes.QuadPart / (1024 * 1024));
						if (freeMB < 500) {
							GetParent()->PostMessage(WM_DV_LOWDISKSPACE, 0, freeMB);
						}
					}
				}
				/* Update dropped-frame delta against the driver's running total. */
				m_dropped = m_dvInput->GetDroppedFrames() - dropped;
			}
			else {
				/* Paused or Finished: close any open writer cleanly. */
				if (m_aviWriter) {
					delete m_aviWriter;
					m_aviWriter = NULL;
				}
				/* Reset the dropped-frame baseline for the next capture segment.
				 * Do not reset if we just finished (preserve final stats). */
				dropped = m_dvInput->GetDroppedFrames();
				if (m_state != Finished) {
					m_dropped = 0;
					m_counter = 0;
					m_time = 0;
				}
			}
		}
	}
	/* Close any writer that was open when the loop exited (e.g. on Destroy()). */
	if (m_aviWriter) {
		delete m_aviWriter;
		m_aviWriter = NULL;
	}
	/* Write capture log CSV if any frames were captured. */
	if (m_counter > 0) {
		capStats.sFilename = m_captureFilename;
		capStats.dwFrameCount = m_counter;
		capStats.dwDroppedFrames = m_dropped;

		/* WDV-10: Copy final error stats into capStats for the CSV log. */
		{
			CAutoLock lock(&m_cs);
			capStats.errorStats = m_errorStats;
		}

		/* Run AVI integrity check on the finished file. */
		m_lastCheckResult = CheckAVIIntegrity(m_captureFilename);
		capStats.bCheckPassed = m_lastCheckResult.bValid
			&& m_lastCheckResult.dwDefectFrames == 0
			&& m_lastCheckResult.bHasIndex;
		capStats.dwCheckDefect = m_lastCheckResult.dwDefectFrames;
		capStats.bCheckIndex = m_lastCheckResult.bHasIndex;

		/* WDV-11: Compute SHA-256 of the finalized AVI file and write sidecar. */
		if (m_enableSHA256) {
			if (ComputeFileSHA256(m_captureFilename, capStats.szSHA256)) {
				/* Extract bare filename for the sidecar line. */
				CString bareName = m_captureFilename;
				int sep = bareName.ReverseFind('\\');
				if (sep >= 0) bareName = bareName.Mid(sep + 1);

				/* Write sidecar next to the capture file. */
				CString sidecarPath;
				sidecarPath.Format("%s.sha256", (LPCSTR)m_captureFilename);
				WriteSHA256Sidecar(sidecarPath, capStats.szSHA256, bareName, FALSE);
			}
		}

		/* Build log path next to the capture file. */
		CString logPath = m_captureFilename;
		int lastSep = logPath.ReverseFind('\\');
		if (lastSep >= 0) logPath = logPath.Left(lastSep + 1);
		else logPath = ".\\";
		logPath += "WinDV_CaptureLog.csv";
		WriteCaptureLog(logPath, capStats);

		/* Notify the UI about the check result. */
		GetParent()->PostMessage(WM_DV_CHECK_COMPLETE,
			(WPARAM)capStats.bCheckPassed, 0);
	}
	/* Notify the UI that timing info is no longer valid (clears the display). */
	GetParent()->PostMessage(WM_DV_TIMECHANGE, 0, 0);
}

/*
 * CDV::RecordingThread
 *
 * Worker thread that consumes frames from CDVQueue and feeds them to CDVOutput
 * (FireWire DV device).
 *
 * Main responsibilities:
 *   1. Parse DV recording timestamps and post WM_DV_TIMECHANGE for UI display.
 *   2. Conditional preview: only deliver frames to CMonitor when m_recordPreview
 *      is set AND (the queue is more than half full OR the queue has ended).
 *      This is the inverse of capture preview throttling: during recording the
 *      queue fills from the file, so preview is sent when there is plenty of
 *      data buffered, not when the queue is nearly empty.
 *   3. Forward every frame to CDVOutput::HandleFrame() while m_state == Recording.
 *      While RecordPaused, frames are still consumed from the queue (to keep the
 *      source running) but are not forwarded to the DV device.
 *   4. Detect end-of-stream: when CDVQueue::Get() returns false, set m_state =
 *      Finished and exit the loop.
 *
 * Loop structure:
 *   The first Get() call outside the inner while loop is intentional: it primes
 *   the buffer variable before entering the loop so that every iteration both
 *   processes the current frame and pre-fetches the next one atomically.
 */
void CDV::RecordingThread()
{
	BYTE *buffer;
	REFERENCE_TIME duration;
	int len;
	long dvTime = 0;
	/* Clear the UI timestamp display at the start of playback. */
	GetParent()->PostMessage(WM_DV_TIMECHANGE, 0, 0);
	if (m_queue->Get(&duration, &buffer, &len)) {
		m_counter = 0;
		m_time  = 0;
		while (m_state) {
			/* Parse and report the recording timestamp from the current frame. */
			long newDVTime = GetDVRecordingTime(buffer, len);
			if (dvTime != newDVTime) {
				dvTime = newDVTime;
				GetParent()->PostMessage(WM_DV_TIMECHANGE, 0, dvTime);
			}
			/* Preview throttle for recording mode: show frames when queue is more
			 * than half full (buffer is healthy) or when the source is exhausted. */
			if (m_recordPreview) {
				if (m_queue->m_end || m_queue->m_load > m_queue->m_queueSize/2)
					m_monitor->HandleFrame(duration, buffer, len);
			}

			/* Forward the frame to the DV output device. */
			m_dvOutput->HandleFrame(duration, buffer, len);
			if (m_state == Recording) {
				m_counter++;
				m_time += duration;
				/* Fetch the next frame; exit if the queue is empty and EOS was set. */
				if (!m_queue->Get(&duration, &buffer, &len)) {
					if (m_state) m_state = Finished;
				}
			}
		}
	}
	/* Clear the UI timestamp display on exit. */
	GetParent()->PostMessage(WM_DV_TIMECHANGE, 0, 0);
}

BEGIN_MESSAGE_MAP(CDV, CStatic)
	//{{AFX_MSG_MAP(CDV)
	ON_WM_SIZE()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////

/*
 * FormatTime
 *
 * Formats a time_t value using strftime with the given format string.
 * Returns an empty string when format is empty (no suffix desired).
 * Used by GetCaptureFilename() to build the date/time portion of AVI filenames.
 *
 * Parameters:
 *   format - strftime format string (e.g. "%Y%m%d_%H%M%S").
 *   tim    - Time value to format.
 *
 * Returns:
 *   Formatted time string, or "" if format is empty.
 */
CString FormatTime(LPCSTR format, time_t tim)
{
	CString tmp = format;
	char buf[1024];

	strftime(buf, sizeof buf, tmp + '\0', localtime(&tim));

	return CString(buf);
}
