import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/foundation.dart';
import '../models/trash_bin_model.dart';

class TrashBinRepository {
  final DatabaseReference _dbRef = FirebaseDatabase.instance.ref();

  /// Stream a single bin's data in real-time
  Stream<TrashBinModel> streamBin(String binId) {
    return _dbRef.child('bins/$binId').onValue.map((event) {
      final data = event.snapshot.value as Map<dynamic, dynamic>? ?? {};
      return TrashBinModel.fromMap(binId, data);
    });
  }

  /// Stream all bins
  Stream<List<TrashBinModel>> streamAllBins() {
    return _dbRef.child('bins').onValue.map((event) {
      final data = event.snapshot.value as Map<dynamic, dynamic>? ?? {};
      return data.entries.map((e) {
        return TrashBinModel.fromMap(
          e.key.toString(),
          e.value as Map<dynamic, dynamic>,
        );
      }).toList();
    });
  }

  /// Clear a specific alert
  Future<void> clearAlert(String binId, String alertKey) async {
    await _dbRef.child('bins/$binId/alerts/$alertKey').set(false);
  }

  /// Clear all alerts for a bin
  Future<void> clearAllAlerts(String binId) async {
    await _dbRef.child('bins/$binId/alerts').update({
      'fire_risk': false,
      'gas_leak': false,
      'bin1_full': false,
      'bin2_full': false,
      'bin3_full': false,
    });
  }

  /// Request robot to follow line to the dump station.
  Future<void> requestGoDump(String binId) async {
    debugPrint('[TrashBinRepository] requestGoDump binId=$binId');
    await _dbRef.child('bins/$binId/commands/go_dump').set(true);
    await _dbRef
        .child('bins/$binId/commands/requested_at')
        .set(ServerValue.timestamp);
  }

  /// Request robot to continue forward on the circular line to the home marker.
  Future<void> requestGoHome(String binId) async {
    debugPrint('[TrashBinRepository] requestGoHome binId=$binId');
    await _dbRef.child('bins/$binId/commands/go_home').set(true);
    await _dbRef
        .child('bins/$binId/commands/requested_at')
        .set(ServerValue.timestamp);
  }

  /// Stop robot movement immediately.
  Future<void> requestStop(String binId) async {
    debugPrint('[TrashBinRepository] requestStop binId=$binId');
    await _dbRef.child('bins/$binId/commands/stop').set(true);
    await _dbRef
        .child('bins/$binId/commands/requested_at')
        .set(ServerValue.timestamp);
  }

  /// Update bin location (from GPS module)
  Future<void> updateLocation(
    String binId,
    double latitude,
    double longitude,
  ) async {
    await _dbRef.child('bins/$binId/location').update({
      'latitude': latitude,
      'longitude': longitude,
    });
  }
}
