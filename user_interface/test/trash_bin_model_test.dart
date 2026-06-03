import 'package:flutter_test/flutter_test.dart';
import 'package:user_interface/app/data/models/trash_bin_model.dart';

void main() {
  test('dump_completed enables home command state', () {
    final bin = TrashBinModel.fromMap('bin_001', {
      'status': {'state': 'dump_completed'},
    });

    expect(bin.isWaitingAtDump, isTrue);
    expect(bin.isDumping, isFalse);
  });

  test('home_completed is recognized as home state', () {
    final bin = TrashBinModel.fromMap('bin_001', {
      'status': {'state': 'home_completed'},
    });

    expect(bin.isHomeCompleted, isTrue);
    expect(bin.isDumping, isFalse);
  });
}
