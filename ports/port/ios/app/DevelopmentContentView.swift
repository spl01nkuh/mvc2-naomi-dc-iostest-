import SwiftUI
import UniformTypeIdentifiers

@MainActor
struct DevelopmentContentView: View {
    @ObservedObject var store: DevelopmentStore
    @Environment(\.scenePhase) private var scenePhase
    @State private var showImporter = false
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
        showImporter || sheet != nil || store.isImporting || scenePhase != .active
    }

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
                Text("DEVELOPMENT BUILD")
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
                Button { showImporter = true } label: {
                    Label("Import ZIP", systemImage: "folder")
                }.disabled(!store.canImport)
                Button { editingControls = true } label: {
                    Label("Layout", systemImage: "hand.draw")
                }.disabled(store.isImporting)
                Button { sheet = .settings } label: {
                    Label("Settings", systemImage: "slider.horizontal.3")
                }
            }
        }
        .font(.subheadline)
        .padding(.horizontal, 16)
        .padding(.vertical, 9)
        .background(.ultraThinMaterial)
    }

    private var developmentNotice: some View {
        VStack(spacing: 8) {
            if store.isImporting {
                ProgressView("Importing and validating ZIP…")
                    .padding(.bottom, 4)
            }
            Text("Development build — native gameplay is not available yet")
                .font(.subheadline.weight(.semibold))
                .multilineTextAlignment(.center)
            Text(editingControls
                 ? "Drag a control to move it. Tap one to change its size or bindings."
                 : "Test ZIP import, touch controls, and saved settings on this device.")
                .font(.caption).foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
            if let failure = store.storageFailure ?? store.importFailure {
                Text(failure).font(.caption).foregroundStyle(.orange)
                    .multilineTextAlignment(.center)
            } else if let message = store.importMessage {
                Text(message).font(.caption).foregroundStyle(.secondary)
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

    private var footer: some View {
        HStack(spacing: 12) {
            Label(store.hasImportedData ? "Data imported" : "No imported data",
                  systemImage: store.hasImportedData ? "checkmark.circle" : "tray")
                .foregroundStyle(store.hasImportedData ? Color.green : Color.secondary)
            Spacer(minLength: 8)
            if editingControls {
                Text("Control layout").foregroundStyle(.secondary)
            } else {
                TimelineView(.periodic(from: .now, by: 0.1)) { _ in
                    let mask = store.input.buttons
                    let titles = NativeButton.allCases
                        .filter { mask & $0.rawValue != 0 }
                        .map(\.title)
                    Text(titles.isEmpty ? "Touch controls test" : "Held: " + titles.joined(separator: " + "))
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
