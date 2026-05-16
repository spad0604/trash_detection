import 'package:get/get.dart';
import '../../../data/models/trash_bin_model.dart';
import '../../../data/repositories/trash_bin_repository.dart';

class DashboardController extends GetxController {
  final TrashBinRepository _repository = TrashBinRepository();

  final Rx<TrashBinModel?> currentBin = Rx<TrashBinModel?>(null);
  final RxList<TrashBinModel> allBins = <TrashBinModel>[].obs;
  final RxBool isLoading = true.obs;
  final RxString selectedBinId = 'bin_001'.obs;

  @override
  void onInit() {
    super.onInit();
    _listenToBin(selectedBinId.value);
    _listenToAllBins();
  }

  void _listenToBin(String binId) {
    _repository.streamBin(binId).listen((bin) {
      currentBin.value = bin;
      isLoading.value = false;
    }, onError: (e) {
      print('[Dashboard] Stream error: $e');
      isLoading.value = false;
    });
  }

  void _listenToAllBins() {
    _repository.streamAllBins().listen((bins) {
      allBins.assignAll(bins);
    });
  }

  void selectBin(String binId) {
    selectedBinId.value = binId;
    isLoading.value = true;
    _listenToBin(binId);
  }

  Future<void> clearAlert(String alertKey) async {
    await _repository.clearAlert(selectedBinId.value, alertKey);
  }

  Future<void> clearAllAlerts() async {
    await _repository.clearAllAlerts(selectedBinId.value);
  }

  // ── Computed getters for UI ─────────────────────────────
  String get statusText {
    final bin = currentBin.value;
    if (bin == null) return 'Offline';
    switch (bin.state) {
      case 'idle':       return 'Sẵn sàng';
      case 'processing': return 'Đang xử lý';
      case 'sorting':    return 'Đang phân loại';
      case 'moving':     return 'Đang di chuyển';
      case 'alert':      return 'Cảnh báo!';
      default:           return 'Offline';
    }
  }

  bool get hasActiveAlert => currentBin.value?.hasAlert ?? false;
}
