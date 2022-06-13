<?php

/**
 * @file
 * Hooks provided by the uc_gc_client module.
 */

/**
 * Alters GoCardless Redirect URL.
 *
 * Allows modules to alter the mandate details array before it is used to
 * compose the GoCardless Redirect URL.
 *
 * @param int $order_id
 *   The Ubercart order ID for the GoCardless mandate that is being created.
 * @param array $mandate_details
 *   The alterable mandate details.
 */
function hook_mandate_details_alter(array &$mandate_details, $order_id) {
  $mandate_details['user']['first_name'] = 'Rob';
}

/**
 * Alters payments details provided to GoCardless.
 *
 * Allows modules to alter the paramaters array for new subscriptions or
 * one-off payments before a call is made to the GoCardless API.
 *
 * @param array $payment_details
 *   The alterable payment details.
 * @param object $order
 *   The Ubercart order for the subscription or payment.
 * @param string $type
 *   The GoCardless API endpoint, either "payment", or "subscription".
 */
function hook_payment_details_alter(array &$payment_details, $order, $type) {
  switch ($type) {
    case 'subscription':
      $payment_details['amount'] = $payment_details['amount'] * 2;
      break;
  }
}

/**
 * Allows you to respond to payment creations.
 *
 * @param object $payment
 *   Object returned by GoCardless from the payment creation.
 * @param int $order_id
 *   The Ubercart order Id.
 * @param string $op
 *   The operation that invoked the hook, either 'checkout', 'scheduled', or
 *   'manual'.
 */
function hook_payment_created($payment, $order_id, $op) {

  backdrop_set_message(t("The payment's status is @status", array(
    '@status' => $payment->status,
  )));
}

/**
 * Alters values before validating a scheduled adjustment.
 *
 * Allows the sheduled adjustment amount, or the order's amount to be altered
 * before validating the adjustment.
 *
 * @param float $amount
 *   The amount of the scheduled adjustment.
 * @param float $price
 *   The total amount for the order.
 * @param string $date
 *   The date of the scheduled adjustment.
 * @param int $order_id
 *   The order ID relating to the scheduled adjustment.
 */
function hook_adjs_validate_alter(&$amount, &$price, $date, $order_id) {
  $amount = 0;
  $price = 0;
}

/**
 * Alters the scheduled "next payment" date before saving to database.
 *
 * During cron runs, after a payment has been created for a "one-off" payments
 * product, the scheduled "next payment" creation date is updated, according
 * to the products scheduling rules. This hook can be used to override the
 * default calculated "next payment" date.    
 *
 * @param int $next_payment
 *   The calculated "next payment" date as a Unix timestamp.
 * @param object $order
 *   The Ubercart order for the subscription or payment.
 */
function hook_next_payment_alter(&$next_payment, $order) {
  if ($order->order_id >= 100) {
    $next_payment = strtotime('+1 month', $next_payment);
  }
}

/**
 * Alters the scheduled "next payment" date before saving to database.
 *
 * Upon completion of a checkout that generated an Instant Payment, the "next
 * payment" date for "one-off" payments products is set to a future
 * date that is determined by the scheduling rules for the product. Use this
 * hook to override the calculated date.   
 *
 * @param int $next_payment
 *   The calculated "next payment" date as a Unix timestamp.
 * @param object $order
 *   The Ubercart order for the subscription or payment.
 */
function hook_billing_request_next_payment_alter(&$next_payment, $order) {
  $next_payment = strtotime('+2 weeks', REQUEST_TIME);
}
