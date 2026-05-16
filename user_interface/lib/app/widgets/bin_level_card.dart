import 'package:flutter/material.dart';
import 'package:google_fonts/google_fonts.dart';
import 'package:percent_indicator/circular_percent_indicator.dart';

import '../theme/app_colors.dart';

class BinLevelCard extends StatelessWidget {
  final String label;
  final int percent;
  final Color color;
  final IconData icon;

  const BinLevelCard({
    super.key,
    required this.label,
    required this.percent,
    required this.color,
    required this.icon,
  });

  @override
  Widget build(BuildContext context) {
    final isFull = percent >= 95;

    return Container(
      padding: const EdgeInsets.all(16),
      decoration: BoxDecoration(
        color: AppColors.surfaceAlt,
        borderRadius: BorderRadius.circular(14),
        border: Border.all(
          color: isFull ? AppColors.danger.withOpacity(0.6) : AppColors.border,
          width: isFull ? 1.5 : 1,
        ),
      ),
      child: Row(
        children: [
          CircularPercentIndicator(
            radius: 36,
            lineWidth: 6,
            percent: (percent / 100).clamp(0.0, 1.0),
            center: Text(
              '$percent%',
              style: GoogleFonts.inter(
                fontSize: 14,
                fontWeight: FontWeight.w700,
                color: AppColors.textPrimary,
              ),
            ),
            progressColor: isFull ? AppColors.danger : color,
            backgroundColor: color.withOpacity(0.15),
            circularStrokeCap: CircularStrokeCap.round,
            animation: true,
            animationDuration: 800,
          ),
          const SizedBox(width: 16),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Icon(icon, size: 16, color: color),
                    const SizedBox(width: 6),
                    Flexible(
                      child: Text(
                        label,
                        style: GoogleFonts.inter(
                          fontSize: 13,
                          fontWeight: FontWeight.w600,
                          color: AppColors.textPrimary,
                        ),
                        overflow: TextOverflow.ellipsis,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 6),
                // Fill bar
                ClipRRect(
                  borderRadius: BorderRadius.circular(4),
                  child: LinearProgressIndicator(
                    value: (percent / 100).clamp(0.0, 1.0),
                    minHeight: 6,
                    backgroundColor: color.withOpacity(0.12),
                    valueColor: AlwaysStoppedAnimation(
                      isFull ? AppColors.danger : color,
                    ),
                  ),
                ),
                const SizedBox(height: 4),
                Text(
                  isFull ? 'ĐẦY — Cần thu gom!' : _levelLabel(percent),
                  style: GoogleFonts.inter(
                    fontSize: 11,
                    fontWeight: FontWeight.w500,
                    color: isFull ? AppColors.danger : AppColors.textMuted,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  String _levelLabel(int p) {
    if (p < 30) return 'Còn trống nhiều';
    if (p < 60) return 'Trung bình';
    if (p < 85) return 'Gần đầy';
    return 'Sắp đầy';
  }
}
