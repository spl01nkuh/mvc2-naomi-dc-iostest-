import SwiftUI
import UniformTypeIdentifiers

@MainActor
struct DevelopmentContentView: View {
    @ObservedObject var store: DevelopmentStore
    @Environment(\.scenePhase) private var scenePhase
    @State private var showImporter = false
    @State private var showOptions = false
    @State private var editingControls = false
    @State private var sheet: HostSheet?

    private enum HostSheet: Identifiable {
        case settings
        case controls
        case control(UUID)

        var id: String {
            switch self {
            case .settings: return "settings"
            case .controls: return "controls"
            case .control(let id): return id.uuidString
            }
        }
    }

    private var overlaySuspended: Bool {
        showImporter || showOptions || sheet != nil || store.isImporting || store.isStopping ||
            scenePhase != .active || store.isAudioInterrupted ||
            (store.isUserPaused && !editingControls)
    }

    private var gameplaySuspended: Bool { overlaySuspended || editingControls }

    var body: some View {
        VStack(spacing: 0) {
            toolbar
            ZStack {
                NativeCanvasView(settings: store.settings, frames: store.frames)
                developmentNotice
                TouchOverlay(settings: $store.settings, input: store.input,
                             editing: editingControls, suspended: overlaySuspended) { id in
                    sheet = .control(id)
                }
                .accessibilityLabel(editingControls ? "Touch control layout editor" : "Touch controls")
            }
            .clipped()
            footer
        }
        .background(Color.black)
        .fileImporter(isPresented: $showImporter, allowedContentTypes: [.zip],
                      allowsMultipleSelection: false) { result in
            switch result {
            case .success(let urls):
                if let url = urls.first { Task { await store.importArchive(url) } }
            case .failure(let error):
                store.reportPickerFailure(error)
            }
        }
        .confirmationDialog("Game options", isPresented: $showOptions, titleVisibility: .visible) {
            if store.gameStatus.isOpen {
                Button("Import ZIP") { showImporter = true }.disabled(!store.canImport)
            }
            Button("Touch Layout") { editingControls = true }.disabled(store.isImporting)
            Button("Settings") { sheet = .settings }
            Button("Cancel", role: .cancel) { }
        }
        .sheet(item: $sheet) { route in
            NavigationStack {
                sheetContent(route)
                    .toolbar {
                        ToolbarItem(placement: .confirmationAction) {
                            Button("Done") { sheet = nil }
                        }
                    }
            }
        }
        .onChange(of: overlaySuspended) { suspended in
            if suspended { store.input.releaseAll() }
        }
        .onChange(of: gameplaySuspended) { store.setHostSuspended($0) }
        .onAppear { store.setHostSuspended(gameplaySuspended) }
        .onDisappear { store.setHostSuspended(true) }
        .onChange(of: editingControls) { _ in store.input.releaseAll() }
        .onChange(of: scenePhase) { phase in
            if phase != .active {
                store.input.releaseAll()
                store.saveSettings()
            }
        }
    }

    private var toolbar: some View {
        HStack(spacing: 16) {
            VStack(alignment: .leading, spacing: 2) {
                Text("MVC2").font(.headline)
                Text("EXPERIMENTAL NATIVE")
                    .font(.system(size: 9, weight: .semibold, design: .rounded))
                    .foregroundStyle(.orange)
            }
            Spacer(minLength: 8)
            if editingControls {
                Text("Drag to move · Tap to edit")
                    .font(.caption).foregroundStyle(.secondary)
                Button("All controls") { sheet = .controls }
                Button("Done") { editingControls = false }
                    .buttonStyle(.borderedProminent)
            } else {
                if store.gameStatus.isOpen {
                    Button(store.isUserPaused ? "Resume" : "Pause") { store.togglePause() }
                        .disabled(!store.canPause || store.isStopping)
                    Button { Task { await store.stopGame() } } label: {
                        Label("Stop Game", systemImage: "stop.fill")
                    }.disabled(store.isStopping)
                        .labelStyle(.iconOnly)
                } else {
                    Button("Start Game") { store.startGame() }
                        .buttonStyle(.borderedProminent)
                        .disabled(!store.canStart)
                }
                if !store.gameStatus.isOpen {
                    Button("Import ZIP") { showImporter = true }
                        .disabled(!store.canImport)
                }
                Button { showOptions = true } label: {
                    Label("Options", systemImage: "ellipsis.circle")
                }.labelStyle(.iconOnly)
            }
        }
        .font(.subheadline)
        .padding(.horizontal, 16)
        .padding(.vertical, 9)
        .background(.ultraThinMaterial)
    }

    private var developmentNotice: some View {
        Group {
            if !store.sessionHasFrame || editingControls || store.gameFailure != nil ||
                store.audioWarning != nil || store.isUserPaused || store.isAudioInterrupted ||
                store.isImporting {
                VStack(spacing: 8) {
                    if store.isImporting {
                        ProgressView("Importing and validating ZIP…")
                    } else if store.gameStatus == .starting {
                        ProgressView("Starting the native engine…")
                    }
                    Text(editingControls ? "Touch control layout" : sessionTitle)
                        .font(.subheadline.weight(.semibold))
                        .multilineTextAlignment(.center)
                    Text(editingControls
                         ? "Drag a control to move it. Tap one to change its size or bindings."
                         : sessionDescription)
                        .font(.caption).foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                    if let failure = store.gameFailure ?? store.storageFailure ?? store.importFailure {
                        Text(failure).font(.caption).foregroundStyle(.orange)
                            .multilineTextAlignment(.center)
                    } else if let message = store.importMessage, !store.gameStatus.isOpen {
                        Text(message).font(.caption).foregroundStyle(.secondary)
                            .multilineTextAlignment(.center)
                    }
                    if let warning = store.audioWarning {
                        Text(warning).font(.caption).foregroundStyle(.orange)
                            .multilineTextAlignment(.center)
                    }
                }
                .padding(12)
                .frame(maxWidth: 500)
                .background(.black.opacity(0.78), in: RoundedRectangle(cornerRadius: 16))
                .padding(.horizontal, 24)
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
                .padding(.top, 10)
                .allowsHitTesting(false)
            }
        }
    }

    private var sessionTitle: String {
        switch store.gameStatus {
        case .stopped: return "Experimental native gameplay"
        case .starting: return "Loading validated game data"
        case .running: return "Native engine session"
        case .paused: return "Game paused"
        case .failed: return "Native engine could not continue"
        }
    }

    private var sessionDescription: String {
        switch store.gameStatus {
        case .stopped:
            return store.hasImportedData
                ? "Start Game boots your validated import. Then use Insert Coin and the on-screen START control. This experimental build still needs device testing."
                : "Import the complete supported NAOMI mvsc2 ZIP, then choose Start Game."
        case .starting:
            return "The first boot can take time. Any engine failure will appear here."
        case .running:
            return store.sessionHasFrame ? "Video comes from the native game renderer."
                : "Waiting for the first frame from the native game renderer."
        case .paused:
            return store.isAudioInterrupted ? "Gameplay and audio are suspended by an audio interruption."
                : "Gameplay and audio are suspended. Close menus or choose Resume to continue."
        case .failed:
            return "The engine reported the error below. You can retry Start Game or import a supported archive again."
        }
    }

    private var footer: some View {
        HStack(spacing: 12) {
            Label(store.hasImportedData ? "Data validated" : "No imported data",
                  systemImage: store.hasImportedData ? "checkmark.circle" : "tray")
                .foregroundStyle(store.hasImportedData ? Color.green : Color.secondary)
            if !editingControls {
                Button("Insert Coin") { store.input.insertCoin() }
                    .disabled(store.gameStatus != .running || gameplaySuspended)
                    .buttonStyle(.bordered)
            }
            Spacer(minLength: 8)
            if editingControls {
                Text("Control layout").foregroundStyle(.secondary)
            } else {
                TimelineView(.periodic(from: .now, by: 0.1)) { _ in
                    let mask = store.input.buttons
                    let titles = NativeButton.allCases
                        .filter { mask & $0.rawValue != 0 }
                        .map(\.title)
                    Text(titles.isEmpty
                         ? (store.gameStatus == .running ? "Insert Coin, then touch START" : sessionTitle)
                         : "Held: " + titles.joined(separator: " + "))
                        .monospaced().lineLimit(1)
                        .foregroundStyle(.secondary)
                }
            }
        }
        .font(.caption)
        .padding(.horizontal, 16)
        .padding(.vertical, 7)
        .background(.ultraThinMaterial)
    }

    @ViewBuilder
    private func sheetContent(_ route: HostSheet) -> some View {
        switch route {
        case .settings: DevelopmentSettingsView(store: store)
        case .controls: ControlsList(settings: $store.settings)
        case .control(let id): ControlDetail(settings: $store.settings, id: id)
        }
    }
}
