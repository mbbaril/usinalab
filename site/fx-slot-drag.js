// SPDX-License-Identifier: GPL-3.0-only
// Slot exchange is committed only on release; cancellation never changes routing.
export function installFxSlotDrag({handle, row, onSelect, onStart, onMove, onDrop, onEnd}) {
  let drag = null, suppressClick = false;
  handle.onclick = () => {
    if (suppressClick) { suppressClick = false; return; }
    onSelect();
  };
  handle.onpointerdown = e => {
    if (drag || e.pointerType === 'mouse' && e.button !== 0) return;
    e.preventDefault();
    suppressClick = false;
    drag = {id:e.pointerId, x:e.clientX, y:e.clientY, moved:false, target:null};
    row.dataset.dragging = 'true';
    handle.setPointerCapture(e.pointerId);
  };
  handle.onpointermove = e => {
    if (drag?.id !== e.pointerId) return;
    e.preventDefault();
    if (!drag.moved) {
      if (Math.hypot(e.clientX-drag.x, e.clientY-drag.y) < 8) return;
      drag.moved = true;
      onStart();
    }
    drag.target = onMove(e);
  };
  const finish = e => {
    if (drag?.id !== e.pointerId) return;
    if (e.type === 'lostpointercapture' && handle.hasPointerCapture(e.pointerId)) return;
    if (e.type === 'pointerup') handle.onpointermove(e);
    const previous = drag;
    drag = null;
    delete row.dataset.dragging;
    if (handle.hasPointerCapture(e.pointerId)) handle.releasePointerCapture(e.pointerId);
    onEnd();
    if (previous.moved) {
      suppressClick = e.type === 'pointerup';
      if (e.type === 'pointerup' && previous.target) onDrop(previous.target);
    }
  };
  handle.onpointerup = handle.onpointercancel = handle.onlostpointercapture = finish;
  return () => {
    if (!drag) return;
    finish({type:'pointercancel', pointerId:drag.id});
  };
}
