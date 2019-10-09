/**
 * @file
 * Utility functions to display settings summaries on vertical tabs.
 */

(function ($) {
  Drupal.behaviors.uc_gcsubsProductFieldsetSummaries = {
    attach: function (context) {
      $('fieldset#edit-gc', context).drupalSetSummary(function (context) {
        var vals = [];
        if (Drupal.checkPlain($('#edit-gc-type', context).val()) == 'P') {
          vals.push(Drupal.t('Type: One-off Payments'))
        }
  else if (Drupal.checkPlain($('#edit-gc-type', context).val()) == 'S') {
          vals.push(Drupal.t('Type: Subscription'));
        }
        if (Drupal.checkPlain($('#edit-interval-length', context).val()) != '') {
          vals.push(Drupal.t('Interval length: ')
            + $('#edit-interval-length', context).val());
        }
        if (Drupal.checkPlain($('#edit-interval-unit', context).val()) != '') {
          vals.push(Drupal.t('Interval unit: ')
            + $('#edit-interval-unit', context).val());
        }
        if (Drupal.checkPlain($('#edit-price-x', context).val()) != '1') {
          vals.push(Drupal.t('Price multiplier: ')
            + Drupal.checkPlain($('#edit-price-x', context).val()));
        }
        if (Drupal.checkPlain($('#edit-start-date-datepicker-popup-0', context).val()) != '') {
          vals.push(Drupal.t('Start date: ') + $('#edit-start-date-datepicker-popup-0', context).val());
        }
        return vals.join(', ');
      });
    }
  };
})(jQuery);
