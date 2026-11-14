import SwiftUI

@main
@MainActor
struct MVC2DevelopmentApp: App {
    @StateObject private var store = DevelopmentStore()

    var body: some Scene {
        WindowGroup {
            DevelopmentContentView(store: store)
                .preferredColorScheme(.dark)
        }
    }
}
