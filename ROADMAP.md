# NeuronOS — Roadmap

## M0: v1.0 Release (Weeks 1-4)

**Goal**: Ship v1.0.0 with documentation good enough for developers to adopt.

### Tasks
- [ ] Write getting-started guide (build from source, first agent, first tool)
- [ ] Document all 12 built-in tools with examples
- [ ] Document memory architecture (Core/Recall/Archival) with usage patterns
- [ ] Document MCP Server integration (how to use with Claude Desktop, VS Code)
- [ ] Document MCP Client (how NeuronOS calls external MCP servers)
- [ ] Document HAL backends — which to use when, benchmark numbers
- [ ] Document GBNF grammar tool calling — how to define schemas
- [ ] Create model compatibility table (tested models, RAM requirements, token/s)
- [ ] Set up docs site (GitHub Pages or Cloudflare Pages, mdBook or similar)
- [ ] Harden Windows CI — add integration tests for Windows
- [ ] Tag v1.0.0, create release notes
- [ ] Write announcement post for Hacker News / Reddit r/LocalLLaMA

### Deliverables
- `docs/` site live at neuronos.dev or similar
- v1.0.0 binaries on GitHub Releases (5 platforms)
- README rewritten for clarity

### Revenue: $0
This month is investment. The goal is GitHub stars and developer awareness.

---

## M1: Pro License System (Weeks 5-8)

**Goal**: Monetize commercial/embedded use with a license key system.

### Tasks
- [ ] Design license key format (offline-verifiable, ed25519 signed, JSON payload)
- [ ] Implement license check in runtime (compile-time flag: `NEURONOS_PRO`)
  - Free builds: full features, MIT license notice on startup
  - Pro builds: license key file check, no notice, commercial use rights
- [ ] Build license key generator (CLI tool, also used by web store)
- [ ] Set up Stripe product: Pro ($99/device/year), Enterprise ($999/year)
- [ ] Landing page at neuronos.dev with pricing, "Buy License" → Stripe Checkout
- [ ] Stripe webhook → generate license key → email to customer
- [ ] Add `neuronos license activate <key>` CLI command
- [ ] Write "Commercial Use" section in docs
- [ ] First outreach: post in embedded/IoT communities, contact hardware makers

### Deliverables
- License key system working end-to-end
- Stripe integration live
- First commercial licenses sold

### Revenue Target: $500-2,000/mo
- 5-20 Pro licenses at $99/year (~$8/mo) = modest start
- 1-2 Enterprise licenses = $999 each
- Focus: IoT companies, robotics startups, privacy-focused orgs

---

## M2: Managed API + Ecosystem (Weeks 9-16)

**Goal**: Offer hosted NeuronOS agents for customers who don't want to self-host.

### Tasks
- [ ] Design managed API: REST endpoints wrapping NeuronOS agent
  - `POST /agents` — create agent with persona + tools
  - `POST /agents/:id/chat` — send message, get agent response (SSE)
  - `GET /agents/:id/memory` — inspect agent memory
- [ ] Deploy on bare-metal/VPS (Hetzner ARM64 — cheap, fast)
- [ ] Implement usage metering (agent steps, tokens, tool calls)
- [ ] Stripe usage-based billing: $0.001/agent step, $0.0001/token
- [ ] Dashboard: usage graphs, billing, API keys
- [ ] Expand model registry: top 50 GGUF models, auto-download
- [ ] Write integration guides: "Use NeuronOS with n8n", "NeuronOS + Home Assistant"
- [ ] Explore GitHub Sponsors for open-source funding

### Deliverables
- Managed API live at api.neuronos.dev
- Usage-based billing working
- 3+ integration guides published

### Revenue Target: $2,000-5,000/mo
- Pro licenses growing: 20-50 at $99/year
- Enterprise: 3-5 at $999/year
- Managed API: early adopters, $100-500/mo each
- GitHub Sponsors: $200-500/mo

---

## M3+: Scale (Months 5-12)

- Marketplace for community tools/agents
- Multi-agent orchestration (agent-to-agent MCP)
- WASM backend for browser-based agents
- Partnerships with hardware manufacturers (pre-installed NeuronOS)
- Target: $10,000-20,000/mo ARR by month 12

---

## Revenue Summary

| Month | Source | Target |
|---|---|---|
| M0 | — | $0 (investment) |
| M1 | Pro/Enterprise licenses | $500-2,000 |
| M2 | Licenses + Managed API | $2,000-5,000 |
| M3-6 | All channels | $5,000-10,000 |
| M6-12 | All channels + partnerships | $10,000-20,000 |
