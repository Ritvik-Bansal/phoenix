import SwiftUI

@main
struct PhoenixApp: App {
    @StateObject private var model = AppModel()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(model)
                .task { model.start() }
        }
    }
}
