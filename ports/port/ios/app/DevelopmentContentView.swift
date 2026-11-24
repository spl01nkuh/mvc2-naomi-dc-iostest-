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

    private var hostSuspended: Bool {
        showImporter || showOptions || sheet != nil || editingControls || scenePhase != .active
    }

    private var touchSuspended: Bool {
        showImporter || showOptions || sheet != nil || store.isImporting || store.isStopping ||
            scenePhase != .active || store.isAudioInterrupted ||
            (!editingControls && (store.isUserPaused || store.gameStatus != .running))
    }

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
            NativeCanvasView(settings: store.settings, frames: store.frames)
                .ignoresSafeArea()

            if store.sessionHasFrame || editingControls || store.showsControlPreview {
                TouchOverlay(settings: $store.settings, input: store.input,
                             editing: editingControls, suspended: touchSuspended) { id in
                    sheet = .control(id)
                }
                .accessibilityLabel(editingControls ? "Touch control layout editor" : "Touch controls")
            }

            notice

            floatingControls
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
                .padding(8)
        }
        .statusBarHidden(true)
        .fileImporter(isPresented: $showImporter, allowedContentTypes: [.zip],
                      allowsMultipleSelection: false) { result in
            switch result {
            case .success(let urls):
                if let url = urls.first { Task { await store.importArchive(url) } }
            case .failure(let error):
                store.reportPickerFailure(error)
            }
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
        .onChange(of: touchSuspended) { suspended in
            if suspended { store.input.releaseAll() }
        }
        .onChange(of: hostSuspended) { store.setHostSuspended($0) }
        .onAppear { store.setHostSuspended(hostSuspended) }
        .onDisappear { store.setHostSuspended(true) }
        .onChange(of: editingControls) { _ in store.input.releaseAll() }
    }

    private var floatingControls: some View {
        HStack(spacing: 8) {
            if editingControls {
                Button("All controls") { sheet = .controls }
                    .buttonStyle(.bordered)
                Button("Done") { editingControls = false }
                    .buttonStyle(.borderedProminent)
            } else {
                if store.gameStatus.isOpen {
                    overlayButton("Insert Coin", icon: "centsign.circle") {
                        _ = store.input.insertCoin()
                    }
                    .disabled(touchSuspended || !store.sessionHasFrame)
                }
                overlayButton("Game options", icon: "ellipsis") {
                    showOptions = true
                }
                .confirmationDialog("Game options", isPresented: $showOptions, titleVisibility: .visible) {
                    if store.canPause {
                        Button(store.isUserPaused ? "Resume" : "Pause") { store.togglePause() }
                    }
                    if store.gameStatus.isOpen {
                        Button("Stop Game") { Task { await store.stopGame() } }
                            .disabled(store.isStopping)
                    }
                    Button("Import ZIP") { showImporter = true }.disabled(!store.canImport)
                    Button("Touch Layout") { editingControls = true }.disabled(store.isImporting)
                    Button("Settings") { sheet = .settings }
                    Button("Cancel", role: .cancel) {}
                }
            }
        }
    }

    private func overlayButton(_ title: String, icon: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: icon)
                .font(.system(size: 19, weight: .semibold))
                .foregroundStyle(.white)
                .frame(width: 44, height: 44)
                .background(.black.opacity(0.42), in: Circle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel(title)
    }

    @ViewBuilder
    private var notice: some View {
        if editingControls {
            Text("Drag a control to move it. Tap to edit.")
                .font(.caption)
                .padding(10)
                .background(.black.opacity(0.75), in: Capsule())
                .frame(maxHeight: .infinity, alignment: .top)
                .padding(.top, 12)
                .allowsHitTesting(false)
        } else if store.isImporting {
            ProgressView("Importing ZIP…")
                .padding(18)
                .background(.black.opacity(0.75), in: RoundedRectangle(cornerRadius: 16))
        } else if !store.sessionHasFrame {
            VStack(spacing: 12) {
                if store.gameStatus.isOpen {
                    ProgressView(store.isAudioInterrupted ? "Game paused" : "Starting game…")
                } else {
                    Text(store.gameFailure == nil ? "MVC2" : "Game could not start")
                        .font(.title2.bold())
                    if let failure = store.gameFailure ?? store.importFailure ?? store.storageFailure {
                        Text(failure)
                            .foregroundStyle(.secondary)
                            .multilineTextAlignment(.center)
                    } else if !store.hasImportedData {
                        Text("Import your complete NAOMI mvsc2 ZIP to start playing.")
                            .foregroundStyle(.secondary)
                    }
                    HStack {
                        if store.canStart {
                            Button("Start Game") { store.startGame() }
                                .buttonStyle(.borderedProminent)
                        }
                        Button("Import ZIP") { showImporter = true }
                            .buttonStyle(.bordered)
                            .disabled(!store.canImport)
                    }
                }
            }
            .padding(16)
            .frame(maxWidth: 460)
        } else if store.isUserPaused || store.isAudioInterrupted {
            VStack(spacing: 10) {
                Text("Game paused").font(.headline)
                if !store.isAudioInterrupted {
                    Button("Resume") { store.togglePause() }
                        .buttonStyle(.borderedProminent)
                }
            }
            .padding(16)
            .background(.black.opacity(0.75), in: RoundedRectangle(cornerRadius: 16))
        } else if let warning = store.audioWarning {
            Text(warning)
                .foregroundStyle(.secondary)
                .font(.caption)
                .multilineTextAlignment(.center)
                .padding(10)
                .background(.black.opacity(0.72), in: Capsule())
                .padding()
                .frame(maxHeight: .infinity, alignment: .bottom)
                .allowsHitTesting(false)
        }
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
