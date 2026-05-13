import { readable, type Readable } from "svelte/store";

export type ConnectionState = "connecting" | "open" | "closed";

const DEFAULT_URL = `ws://${location.hostname || "localhost"}:8765`;
const BACKOFF_INITIAL_MS = 1_000;
const BACKOFF_MAX_MS = 30_000;

interface ClientMessage {
  action: "subscribe" | "unsubscribe";
  topic: string;
}

type ServerMessage =
  | { topic: string; data: unknown }
  | { error: string; topic?: string };

class WrapperBus {
  private socket: WebSocket | null = null;
  private state: ConnectionState = "connecting";
  private stateListeners = new Set<(s: ConnectionState) => void>();
  private topicListeners = new Map<string, Set<(value: unknown) => void>>();
  private latest = new Map<string, unknown>();
  private backoffMs = BACKOFF_INITIAL_MS;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;

  constructor(private readonly url: string) {
    this.connect();
  }

  subscribeState(listener: (s: ConnectionState) => void): () => void {
    listener(this.state);
    this.stateListeners.add(listener);
    return () => this.stateListeners.delete(listener);
  }

  subscribeTopic(
    topic: string,
    listener: (value: unknown) => void,
  ): () => void {
    let listeners = this.topicListeners.get(topic);
    if (!listeners) {
      listeners = new Set();
      this.topicListeners.set(topic, listeners);
      this.send({ action: "subscribe", topic });
    }
    listeners.add(listener);
    const cached = this.latest.get(topic);
    if (cached !== undefined) listener(cached);
    return () => {
      const set = this.topicListeners.get(topic);
      if (!set) return;
      set.delete(listener);
      if (set.size === 0) {
        this.topicListeners.delete(topic);
        this.latest.delete(topic);
        this.send({ action: "unsubscribe", topic });
      }
    };
  }

  private connect(): void {
    this.setState("connecting");
    const socket = new WebSocket(this.url);
    this.socket = socket;

    socket.addEventListener("open", () => {
      this.setState("open");
      this.backoffMs = BACKOFF_INITIAL_MS;
      for (const topic of this.topicListeners.keys()) {
        this.send({ action: "subscribe", topic });
      }
    });

    socket.addEventListener("message", (event) => {
      this.onMessage(event.data);
    });

    socket.addEventListener("close", () => {
      this.setState("closed");
      this.socket = null;
      this.scheduleReconnect();
    });

    socket.addEventListener("error", () => {
      socket.close();
    });
  }

  private scheduleReconnect(): void {
    if (this.reconnectTimer !== null) return;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.backoffMs = Math.min(this.backoffMs * 2, BACKOFF_MAX_MS);
      this.connect();
    }, this.backoffMs);
  }

  private setState(state: ConnectionState): void {
    this.state = state;
    for (const listener of this.stateListeners) listener(state);
  }

  private send(message: ClientMessage): void {
    if (this.socket?.readyState === WebSocket.OPEN) {
      this.socket.send(JSON.stringify(message));
    }
  }

  private onMessage(raw: unknown): void {
    if (typeof raw !== "string") return;
    let parsed: ServerMessage;
    try {
      parsed = JSON.parse(raw) as ServerMessage;
    } catch {
      console.warn("wrapper-bus: malformed frame", raw);
      return;
    }
    if ("error" in parsed) {
      console.warn("wrapper-bus: server error", parsed);
      return;
    }
    const listeners = this.topicListeners.get(parsed.topic);
    if (!listeners) return;
    this.latest.set(parsed.topic, parsed.data);
    for (const listener of listeners) listener(parsed.data);
  }
}

const bus = new WrapperBus(DEFAULT_URL);

export const connectionState: Readable<ConnectionState> =
  readable<ConnectionState>("connecting", (set) => bus.subscribeState(set));

export function topic<T>(name: string): Readable<T | null> {
  return readable<T | null>(null, (set) => {
    return bus.subscribeTopic(name, (value) => {
      set(value as T);
    });
  });
}
