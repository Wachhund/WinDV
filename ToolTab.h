#if !defined(AFX_TOOLTAB_H__AB98922A_AE7E_4572_8094_F2F1BCE5E107__INCLUDED_)
#define AFX_TOOLTAB_H__AB98922A_AE7E_4572_8094_F2F1BCE5E107__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// ToolTab.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CToolTab
//
// Owner-draw CTabCtrl subclass used as the mode selector in the main dialog.
//
// The standard CTabCtrl renders tab items with a raised 3D border that clashes
// with the dialog background colour.  CToolTab overrides DrawItem() to fill
// each tab rectangle with the parent dialog's background brush (obtained via
// WM_CTLCOLORDLG) and then draw the label text centred, producing a flat look
// that integrates better with the dark preview area.
//
// The tab control must have the TCS_OWNERDRAWFIXED style set in the resource
// editor for DrawItem() to be called.
//
// Tabs managed by this control:
//   Index 0 — "Video Capture"  (IDS_TAB_VIDEO_CAPTURE)
//   Index 1 — "Video Recording" (IDS_TAB_VIDEO_RECORDING)
//

class CToolTab : public CTabCtrl
{
// Construction
public:
	CToolTab();

// Attributes
public:

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CToolTab)
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CToolTab();

protected:
	/* DrawItem -- called by the framework for each tab that needs repainting.
	 * Fills the item rectangle with the parent's dialog background colour and
	 * draws the item's label text centred within the rectangle.
	 * lpDrawItemStruct  Pointer to the DRAWITEMSTRUCT describing the item to paint.
	 */
	void DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct);

};

/////////////////////////////////////////////////////////////////////////////

#endif // !defined(AFX_TOOLTAB_H__AB98922A_AE7E_4572_8094_F2F1BCE5E107__INCLUDED_)
