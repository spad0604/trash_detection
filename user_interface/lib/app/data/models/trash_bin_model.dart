class TrashBinModel {
  final String id;
  final String name;
  final double temperature1;
  final double humidity1;
  final double temperature2;
  final double humidity2;
  final double temperature3;
  final double humidity3;
  final int mq2_1;
  final int mq2_2;
  final int mq2_3;
  final int mq135_1;
  final int mq135_2;
  final int mq135_3;
  final int bin1Percent;
  final int bin2Percent;
  final int bin3Percent;
  final double batteryVoltage;
  final int batteryPercent;
  final String state;
  final String lastClassification;
  final double? latitude;
  final double? longitude;
  final bool fireRisk;
  final bool gasLeak;
  final bool bin1Full;
  final bool bin2Full;
  final bool bin3Full;
  final bool goDumpRequested;
  final bool goHomeRequested;
  final int? lastUpdate;

  TrashBinModel({
    required this.id,
    this.name = 'Smart Bin',
    this.temperature1 = 0,
    this.humidity1 = 0,
    this.temperature2 = 0,
    this.humidity2 = 0,
    this.temperature3 = 0,
    this.humidity3 = 0,
    this.mq2_1 = 0,
    this.mq2_2 = 0,
    this.mq2_3 = 0,
    this.mq135_1 = 0,
    this.mq135_2 = 0,
    this.mq135_3 = 0,
    this.bin1Percent = 0,
    this.bin2Percent = 0,
    this.bin3Percent = 0,
    this.batteryVoltage = 0,
    this.batteryPercent = 0,
    this.state = 'offline',
    this.lastClassification = '',
    this.latitude,
    this.longitude,
    this.fireRisk = false,
    this.gasLeak = false,
    this.bin1Full = false,
    this.bin2Full = false,
    this.bin3Full = false,
    this.goDumpRequested = false,
    this.goHomeRequested = false,
    this.lastUpdate,
  });

  factory TrashBinModel.fromMap(String id, Map<dynamic, dynamic> map) {
    final sensors = map['sensors'] as Map<dynamic, dynamic>? ?? {};
    final levels = map['levels'] as Map<dynamic, dynamic>? ?? {};
    final battery = map['battery'] as Map<dynamic, dynamic>? ?? {};
    final status = map['status'] as Map<dynamic, dynamic>? ?? {};
    final location = map['location'] as Map<dynamic, dynamic>? ?? {};
    final alerts = map['alerts'] as Map<dynamic, dynamic>? ?? {};
    final commands = map['commands'] as Map<dynamic, dynamic>? ?? {};

    return TrashBinModel(
      id: id,
      name: (map['name'] as String?) ?? 'Smart Bin $id',
      temperature1: (sensors['temperature1'] as num?)?.toDouble() ?? 0,
      humidity1: (sensors['humidity1'] as num?)?.toDouble() ?? 0,
      temperature2: (sensors['temperature2'] as num?)?.toDouble() ?? 0,
      humidity2: (sensors['humidity2'] as num?)?.toDouble() ?? 0,
      temperature3: (sensors['temperature3'] as num?)?.toDouble() ?? 0,
      humidity3: (sensors['humidity3'] as num?)?.toDouble() ?? 0,
      mq2_1: (sensors['mq2_1'] as num?)?.toInt() ?? 0,
      mq2_2: (sensors['mq2_2'] as num?)?.toInt() ?? 0,
      mq2_3: (sensors['mq2_3'] as num?)?.toInt() ?? 0,
      mq135_1: (sensors['mq135_1'] as num?)?.toInt() ?? 0,
      mq135_2: (sensors['mq135_2'] as num?)?.toInt() ?? 0,
      mq135_3: (sensors['mq135_3'] as num?)?.toInt() ?? 0,
      bin1Percent: (levels['bin1_percent'] as num?)?.toInt() ?? 0,
      bin2Percent: (levels['bin2_percent'] as num?)?.toInt() ?? 0,
      bin3Percent: (levels['bin3_percent'] as num?)?.toInt() ?? 0,
      batteryVoltage: (battery['voltage'] as num?)?.toDouble() ?? 0,
      batteryPercent: (battery['percent'] as num?)?.toInt() ?? 0,
      state: (status['state'] as String?) ?? 'offline',
      lastClassification: (status['last_classification'] as String?) ?? '',
      latitude: (location['latitude'] as num?)?.toDouble(),
      longitude: (location['longitude'] as num?)?.toDouble(),
      fireRisk: (alerts['fire_risk'] as bool?) ?? false,
      gasLeak: (alerts['gas_leak'] as bool?) ?? false,
      bin1Full: (alerts['bin1_full'] as bool?) ?? false,
      bin2Full: (alerts['bin2_full'] as bool?) ?? false,
      bin3Full: (alerts['bin3_full'] as bool?) ?? false,
      goDumpRequested: (commands['go_dump'] as bool?) ?? false,
      goHomeRequested: (commands['go_home'] as bool?) ?? false,
      lastUpdate: (status['last_update'] as num?)?.toInt(),
    );
  }

  double get avgTemperature => (temperature1 + temperature2 + temperature3) / 3;
  double get avgHumidity => (humidity1 + humidity2 + humidity3) / 3;
  int get maxMq2 => [mq2_1, mq2_2, mq2_3].reduce((a, b) => a > b ? a : b);
  int get maxMq135 =>
      [mq135_1, mq135_2, mq135_3].reduce((a, b) => a > b ? a : b);
  bool get hasAlert => fireRisk || gasLeak || bin1Full || bin2Full || bin3Full;
  bool get isOnline => state != 'offline';
  bool get isDumping =>
      state == 'dump_requested' ||
      state == 'dump_outbound' ||
      state == 'home_requested' ||
      state == 'dump_returning';
  bool get isWaitingAtDump => state == 'awaiting_return' || state == 'dump_completed';
  bool get isHomeCompleted => state == 'home_completed';
}
