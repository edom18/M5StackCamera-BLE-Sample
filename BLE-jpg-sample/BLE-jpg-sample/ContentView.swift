//
//  ContentView.swift
//  BLE-jpg-sample
//
//  Created by Kazuya Hiruma on 2025/10/08.
//

import SwiftUI

struct ContentView: View {
    
    @StateObject private var ble = BLEJpegSample()
    
    var body: some View {
        VStack(spacing: 16) {
            Text("BLE Status: \(ble.connectionState.rawValue)")
            Text(ble.statusMessage)
                .font(.footnote)
                .foregroundStyle(.secondary)
            
            if let image = ble.lastImage {
                Image(uiImage: image)
                    .resizable()
                    .scaledToFit()
                    .frame(maxHeight: 240)
                    .border(.gray)
            }
            else {
                Rectangle()
                    .fill(.gray.opacity(0.1))
                    .frame(height: 240)
                    .overlay(Text("No image yet").foregroundStyle(.secondary))
            }
            
            HStack {
                Button("Start Scane") {
                    ble.start()
                }
                Button("Stop") {
                    ble.stop()
                }
                Button("Requet JPEG") {
                    ble.requestJpegTransfer()
                }
                .disabled(ble.connectionState != .subscribed)
            }
        }
        .padding()
        .onDisappear {
            ble.stop()
        }
    }
}

#Preview {
    ContentView()
}
