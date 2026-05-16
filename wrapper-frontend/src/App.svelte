<script lang="ts">
  import { onMount } from "svelte";
  import { connectionState, topic } from "./lib/wrapper-bus";

  let subscribed = $state(false);
  const wrapperPacket = topic<Record<string, unknown>>("wrapper_packet.out");

  const snapshotBase = `http://${location.hostname}:8765/snapshot/0`;
  let cacheBuster = $state(0);

  onMount(() => {
    const intervalId = setInterval(() => {
      cacheBuster = Date.now();
    }, 1000);
    return () => {
      clearInterval(intervalId);
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
    <h2>Camera 0</h2>
    <img src={`${snapshotBase}?t=${String(cacheBuster)}`} alt="camera 0 quad" />
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

  img {
    max-width: 100%;
    height: auto;
    display: block;
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
