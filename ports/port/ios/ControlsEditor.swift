import Foundation
import SwiftUI
import UIKit

struct PercentSlider: View {
    var title: String
    @Binding var value: Double
    var range: ClosedRange<Double>
    var body: some View {
        VStack(alignment: .leading) {
            HStack { Text(title); Spacer(); Text("\(Int((value * 100).rounded()))%").monospacedDigit().foregroundStyle(.secondary) }
            Slider(value: $value, in: range, step: 0.01).accessibilityLabel(title)
        }
    }
}

struct ControlsList: View {
    @Binding var settings: AppSettings
    var body: some View {
        List {
            Section {
                ForEach(settings.controls) { control in
                    NavigationLink { ControlDetail(settings: $settings, id: control.id) } label: {
                        HStack { Text(control.label); Spacer(); Text("\(Int((control.size * 100).rounded()))%").foregroundStyle(.secondary) }
                    }
                }
            } footer: { Text("Custom buttons hold all selected bindings simultaneously. Up to 16 custom buttons are supported.") }
            Section {
                Button("Add custom button") {
                    settings.controls.append(TouchControl(label: "CUSTOM", x: 0.55, y: 0.5, buttons: NativeButton.lp.rawValue | NativeButton.hp.rawValue, custom: true))
                }.disabled(settings.controls.filter(\.custom).count >= 16)
            }
        }.navigationTitle("Controls")
    }
}

struct ControlDetail: View {
    @Binding var settings: AppSettings
    var id: UUID
    @Environment(\.dismiss) private var dismiss
    @FocusState private var sizeFocused: Bool
    @State private var sizeText = ""
    private var index: Int? { settings.controls.firstIndex { $0.id == id } }
    private var control: Binding<TouchControl> {
        Binding(get: { index.map { settings.controls[$0] } ?? TouchControl(label: "Removed", x: 0.5, y: 0.5, buttons: 0) }, set: { if let index = index { settings.controls[index] = $0 } })
    }
    var body: some View {
        Form {
            if index != nil {
                Section("Appearance") {
                    TextField("Label", text: control.label)
                        .onChange(of: control.wrappedValue.label) { value in if value.count > 12 { control.wrappedValue.label = String(value.prefix(12)) } }
                    PercentSlider(title: "Size", value: control.size, range: 0.55...2)
                    HStack {
                        Text("Exact size (%)")
                        TextField("100", text: $sizeText)
                            .keyboardType(.decimalPad).multilineTextAlignment(.trailing).focused($sizeFocused)
                            .onSubmit { commitSize() }
                    }
                    PercentSlider(title: "Horizontal position", value: control.x, range: 0...1)
                    PercentSlider(title: "Vertical position", value: control.y, range: 0...1)
                }
                if !control.wrappedValue.isDirection {
                    Section("Hold these bindings together") {
                        ForEach(NativeButton.allCases) { button in
                            Toggle(button.title, isOn: Binding(get: { control.wrappedValue.buttons & button.rawValue != 0 }, set: { enabled in
                                if enabled { control.wrappedValue.buttons |= button.rawValue }
                                else { control.wrappedValue.buttons &= ~button.rawValue }
                            }))
                        }
                    }
                    if control.wrappedValue.buttons == 0 { Text("Choose at least one binding to activate this button.").foregroundStyle(.orange) }
                }
                if control.wrappedValue.custom {
                    Section { Button("Delete custom button", role: .destructive) { settings.controls.removeAll { $0.id == id }; dismiss() } }
                }
            }
        }
        .navigationTitle(control.wrappedValue.label)
        .onAppear { refreshSizeText() }
        .onDisappear {
            commitSize()
            let trimmed = control.wrappedValue.label.trimmingCharacters(in: .whitespacesAndNewlines)
            control.wrappedValue.label = trimmed.isEmpty ? (control.wrappedValue.isDirection ? "Direction" : "CUSTOM") : String(trimmed.prefix(12))
        }
        .onChange(of: control.wrappedValue.size) { _ in if !sizeFocused { refreshSizeText() } }
        .onChange(of: sizeFocused) { focused in if !focused { commitSize() } }
        .toolbar { ToolbarItemGroup(placement: .keyboard) { Spacer(); Button("Done") {
            commitSize(); sizeFocused = false
            UIApplication.shared.sendAction(#selector(UIResponder.resignFirstResponder), to: nil, from: nil, for: nil)
        } } }
    }

    private func refreshSizeText() {
        sizeText = NumberFormatter.localizedString(from: NSNumber(value: control.wrappedValue.size * 100), number: .decimal)
    }
    private func commitSize() {
        let formatter = NumberFormatter()
        formatter.numberStyle = .decimal
        if let value = formatter.number(from: sizeText)?.doubleValue, value.isFinite {
            control.wrappedValue.size = min(200, max(55, value)) / 100
        }
        refreshSizeText()
    }
}
