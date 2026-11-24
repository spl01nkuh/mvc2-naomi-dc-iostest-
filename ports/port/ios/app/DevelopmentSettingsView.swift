import SwiftUI

@MainActor
struct DevelopmentSettingsView: View {
    @ObservedObject var store: DevelopmentStore

    var body: some View {
        Form {
            Section {
                Text("Experimental native gameplay build")
                    .font(.headline)
                Text("Gameplay and audio pause while settings are open.")
                    .foregroundStyle(.secondary)
            }

            Section("Game data") {
                LabeledContent("Status", value: store.hasImportedData ? "Imported and validated" : "Not imported")
                LabeledContent("Storage", value: store.storageLocation)
                    .font(.caption).textSelection(.enabled)
                Text("Choose the complete supported NAOMI mvsc2 archive from Files using Import ZIP. The game starts automatically after validation.")
                    .font(.caption).foregroundStyle(.secondary)
                if let failure = store.storageFailure {
                    Text(failure).foregroundStyle(.orange)
                }
            }

            Section {
                Picker("Preset", selection: $store.settings.filter) {
                    ForEach(CRTFilter.allCases) { filter in Text(filter.title).tag(filter) }
                }
                PercentSlider(title: "Strength", value: $store.settings.filterStrength, range: 0...1)
                    .disabled(store.settings.filter == .off)
            } header: {
                Text("CRT filter")
            } footer: {
                Text("Display settings are saved. A gameplay image is required to see their effect.")
            }

            Section("Screen") {
                Toggle("Native widescreen (16:9)", isOn: $store.settings.nativeWidescreen)
                Text("Widens supported 3D fights. Title, selection, and menus keep their original framing.")
                    .font(.caption).foregroundStyle(.secondary)
                Picker("Aspect ratio", selection: $store.settings.aspect) {
                    ForEach(ScreenAspect.allCases) { aspect in
                        Text(store.settings.nativeWidescreen && aspect == .fourThree
                             ? "Fit game view" : aspect.title).tag(aspect)
                    }
                }
                PercentSlider(title: "Overall size", value: $store.settings.screenScale, range: 0.25...1)
                PercentSlider(title: "Width", value: $store.settings.screenWidth, range: 0.25...2)
                PercentSlider(title: "Height", value: $store.settings.screenHeight, range: 0.25...2)
                PercentSlider(title: "Horizontal offset", value: $store.settings.screenOffsetX, range: -1...1)
                PercentSlider(title: "Vertical offset", value: $store.settings.screenOffsetY, range: -1...1)
                Button("Reset screen geometry") {
                    store.settings.aspect = .fourThree
                    store.settings.screenScale = 1
                    store.settings.screenWidth = 1
                    store.settings.screenHeight = 1
                    store.settings.screenOffsetX = 0
                    store.settings.screenOffsetY = 0
                }
            }

            Section("Touch controls") {
                Toggle("Show touch controls", isOn: $store.settings.touchEnabled)
                PercentSlider(title: "Opacity", value: $store.settings.touchOpacity, range: 0.1...1)
                Picker("Direction control", selection: $store.settings.directionStyle) {
                    ForEach(DirectionStyle.allCases) { style in Text(style.title).tag(style) }
                }
                Toggle("Transparent D-pad / stick", isOn: $store.settings.transparentDirections)
                PercentSlider(title: "Stick sensitivity", value: $store.settings.stickSensitivity, range: 0.01...1)
                    .disabled(store.settings.directionStyle == .dpad)
                PercentSlider(title: "Stick dead zone", value: $store.settings.stickDeadZone, range: 0...0.35)
                    .disabled(store.settings.directionStyle == .dpad)
                NavigationLink("Buttons and bindings") { ControlsList(settings: $store.settings) }
                Text("Use Touch Layout in the game menu to move controls. Custom buttons can hold several bindings at once.")
                    .font(.caption).foregroundStyle(.secondary)
            }

            if let failure = store.settingsFailure {
                Section { Text(failure).foregroundStyle(.orange) }
            }
        }
        .navigationTitle("Settings")
    }
}
