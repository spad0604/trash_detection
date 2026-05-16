import 'package:flutter/material.dart';
import 'package:google_fonts/google_fonts.dart';

import '../theme/app_colors.dart';

class BatteryIndicator extends StatelessWidget {
  final int percent;
  final double voltage;

  const BatteryIndicator({
    super.key,
    required this.percent,
    required this.voltage,
  });

  @override
  Widget build(BuildContext context) {
    final Color color;
    final IconData icon;

    if (percent > 60) {
      color = AppColors.success;
      icon = Icons.battery_full_rounded;
    } else if (percent > 20) {
      color = AppColors.warning;
      icon = Icons.battery_3_bar_rounded;
    } else {
      color = AppColors.danger;
      icon = Icons.battery_alert_rounded;
    }

    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      decoration: BoxDecoration(
        color: color.withOpacity(0.1),
        borderRadius: BorderRadius.circular(8),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icon, size: 16, color: color),
          const SizedBox(width: 4),
          Text(
            '$percent%',
            style: GoogleFonts.inter(
              fontSize: 12,
              fontWeight: FontWeight.w600,
              color: color,
            ),
          ),
        ],
      ),
    );
  }
}
