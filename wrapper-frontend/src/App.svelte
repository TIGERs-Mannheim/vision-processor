<script lang="ts">
  import { onMount } from "svelte";
  import { connectionState, topic } from "./lib/wrapper-bus";

  let subscribed = $state(false);
  const wrapperPacket = topic<Record<string, unknown>>("wrapper_packet.out");

  const apiBase = `http://${location.hostname}:8765`;
  interface Snapshot {
    cam_id: string;
    view: string;
  }
  let snapshots = $state<Snapshot[]>([]);
  let cacheBuster = $state(0);

  async function refreshSnapshotList(): Promise<void> {
    try {
      const response = await fetch(`${apiBase}/snapshots`);
      if (response.ok) snapshots = (await response.json()) as Snapshot[];
    } catch {
      // ignore: next tick will retry
    }
  }

  onMount(() => {
    void refreshSnapshotList();
    const listId = setInterval(() => void refreshSnapshotList(), 5000);
    const imgId = setInterval(() => {
      cacheBuster = Date.now();
    }, 1000);
    return () => {
      clearInterval(listId);
      clearInterval(imgId);
    };
  });

  function toggleSubscribe(): void {
    subscribed = !subscribed;
  }
</script>

<main>
  <header>
    <h1>vision-processor wrapper</h1>
    <span class="badge" data-state={$connectionState}>
      {$connectionState}
    </span>
  </header>

  <section>
    <h2>Snapshots</h2>
    {#if snapshots.length === 0}
      <p class="hint">No images in img/ yet.</p>
    {:else}
      <div class="grid">
        {#each snapshots as snap (`${snap.cam_id}.${snap.view}`)}
          <figure>
            <img
              src={`${apiBase}/snapshot/${snap.cam_id}/${snap.view}?t=${String(cacheBuster)}`}
              alt={`cam ${snap.cam_id} ${snap.view}`}
            />
            <figcaption>cam {snap.cam_id} / {snap.view}</figcaption>
          </figure>
        {/each}
      </div>
    {/if}
  </section>

  <section>
    <button onclick={toggleSubscribe}>
      {subscribed ? "Unsubscribe" : "Subscribe to wrapper_packet.out"}
    </button>

    {#if subscribed}
      {#if $wrapperPacket}
        <pre>{JSON.stringify($wrapperPacket, null, 2)}</pre>
      {:else}
        <p class="hint">Waiting for first frame…</p>
      {/if}
    {/if}
  </section>

  <footer>wrapper-frontend dev skeleton</footer>
</main>

<style>
  main {
    max-width: 1024px;
    margin: 0 auto;
    padding: 1.5rem;
    font-family: ui-sans-serif, system-ui, sans-serif;
  }

  header {
    display: flex;
    align-items: center;
    gap: 1rem;
    margin-bottom: 1rem;
  }

  h1 {
    font-size: 1.25rem;
    margin: 0;
  }

  .badge {
    font-size: 0.75rem;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    padding: 0.2rem 0.5rem;
    border-radius: 999px;
    background: #ddd;
    color: #333;
  }

  .badge[data-state="open"] {
    background: #c8e6c9;
    color: #1b5e20;
  }

  .badge[data-state="connecting"] {
    background: #fff3cd;
    color: #856404;
  }

  .badge[data-state="closed"] {
    background: #f8d7da;
    color: #721c24;
  }

  .grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 0.75rem;
  }

  figure {
    margin: 0;
  }

  img {
    width: 100%;
    height: auto;
    display: block;
    background: #f5f5f5;
  }

  figcaption {
    font-size: 0.75rem;
    color: #666;
    margin-top: 0.25rem;
  }

  h2 {
    font-size: 1rem;
    margin: 0 0 0.5rem;
  }

  pre {
    background: #f5f5f5;
    padding: 1rem;
    border-radius: 4px;
    overflow: auto;
    max-height: 70vh;
    font-size: 0.8rem;
  }

  .hint {
    color: #666;
    font-style: italic;
  }

  footer {
    margin-top: 2rem;
    color: #888;
    font-size: 0.75rem;
  }
</style>
