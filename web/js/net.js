// The WebSocket link to the server, with automatic reconnection. Text frames
// are JSON messages; binary frames are snapshots.

export class Link {
  constructor({ onMessage, onSnapshot, onOpen, onClose }) {
    this.onMessage = onMessage;
    this.onSnapshot = onSnapshot;
    this.onOpen = onOpen;
    this.onClose = onClose;
    this.socket = null;
    this.retry = 0;
    this.open = false;
    this.closedByUs = false;
  }

  url() {
    const scheme = location.protocol === 'https:' ? 'wss:' : 'ws:';
    return `${scheme}//${location.host}/ws`;
  }

  connect() {
    this.closedByUs = false;
    const socket = new WebSocket(this.url());
    socket.binaryType = 'arraybuffer';
    this.socket = socket;
    socket.addEventListener('open', () => {
      this.open = true;
      this.retry = 0;
      this.onOpen?.();
    });
    socket.addEventListener('message', (event) => {
      if (typeof event.data === 'string') {
        let message;
        try {
          message = JSON.parse(event.data);
        } catch {
          return;
        }
        this.onMessage?.(message);
      } else {
        this.onSnapshot?.(event.data);
      }
    });
    socket.addEventListener('close', () => {
      const wasOpen = this.open;
      this.open = false;
      if (this.socket !== socket) return;
      this.onClose?.(wasOpen);
      if (this.closedByUs) return;
      const delay = Math.min(5000, 400 * 2 ** this.retry++);
      setTimeout(() => this.connect(), delay);
    });
  }

  send(message) {
    if (this.open && this.socket.readyState === WebSocket.OPEN) {
      this.socket.send(JSON.stringify(message));
      return true;
    }
    return false;
  }
}
