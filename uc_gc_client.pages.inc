<?php

/**
 * @file
 * Functions that handle redirections and webhooks from GoCardless.com.
 */

/**
 * Menu callback; complete an order on redirection back from GoCardless.com.
 */
function uc_gc_client_redirect($redirect, $mandate, $customer, $order_id, $payment_method, $start_date = NULL, $cart_id = 0) {

  if (intval($_SESSION['cart_order']) != $order_id) {
    drupal_set_message(t('Thank you for your order! GoCardless will notify us once your payment has been processed.'));
    drupal_goto('cart');
  }

  // Ensure the payment method is GoCardless.
  if ($payment_method != 'gc_client') {
    drupal_goto('cart');
  }

  $order = uc_order_load($order_id);
  $uid = $order->uid;

  // Let customer know mandate has been created.
  $comment = t('Your new direct debit mandate @mandate has been created by GoCardless.', array('@mandate' => $mandate));
  uc_order_comment_save($order_id, $uid, $comment, 'order', 'Pending', TRUE);
  drupal_set_message($comment);


  // @todo Apply some logic here to group products into single payments / subscriptions if applicable

  // Set up seperate payments / subscriptions for each product in cart.
  foreach ($order->products as $product) {

    $ucpid = $product->order_product_id;
    $product->data['gc_auth_type'] == 'subscription' ? $gc_type = 'S' : $gc_type = 'P';

    // Obtain start date.
    $product_uc = db_select('uc_gc_client_products', 'p')
      ->fields('p')
      ->condition('nid', $product->nid)
      ->execute()->fetch();

    if ($product_uc->raise_payment) {
      $start_date = REQUEST_TIME;
    }
    else {
      // TODO should this be >= $next_possible_charge_date ?
      if ($product_uc->start_date >= REQUEST_TIME) {
        $start_date = $product_uc->start_date;
      }
      else {
        if (variable_get('uc_gc_client_dom')) {
          $start_date = uc_gc_client_start_date();
        }
        else {
          $start_date = NULL;
        }
      }
      if ($start_date) {
        $params = array(
          'endpoint' => 'mandates',
          'action' => 'get',
          'mandate' => $mandate,
        );
        $result = uc_gc_client_api($params);
        $next_possible_charge_date = $result->response->body->mandates->next_possible_charge_date;

        if ($start_date < strtotime($next_possible_charge_date)) {
          $start_date = strtotime($next_possible_charge_date);
        }
      }
    }

    // Insert info about the order into the database.
    $data = array(
      'ucid'   => $order_id,
      'ucpid'  => $ucpid,
      'gcid'   => $mandate,
      'gcrid'  => $redirect,
      'gccid'  => $customer,
      'uid'    => $uid,
      'type'   => $gc_type,
      'status' => 'pending',
      'created' => REQUEST_TIME,
      'start_date' => $start_date,
      'updated' => REQUEST_TIME,
      'sandbox' => variable_get('uc_gc_client_sandbox', TRUE),
    );
    $db_insert = db_insert('uc_gc_client')
      ->fields($data)
      ->execute();

    isset($product->data['interval_params']) ? $interval = $product->data['interval_params'] : NULL;

    // If it is a subscription.
    if ($gc_type == 'S') {

      $calculate = uc_gc_client_price_calculate($order, $product->order_product_id, $next_paymentdate = NULL, $status = NULL);
      $currency_code = isset($calculate['currency']) ? $calculate['currency'] : variable_get('uc_currency_code');
      $currency_sign = isset($calculate['sign']) ? $calculate['sign'] : variable_get('uc_currency_sign');

      $payment_details = array(
        'amount' => $calculate['amount'],
        'currency' => $currency_code,
        'name' => 'Subscription plan for ' . $product->title,
        'interval' => $interval['length'],
        'interval_unit' => $interval['unit_gc'],
        'metadata' => array(
          'ucpid' => $ucpid,
        ),
      );
      // Provide hook so that payment details can be altered by another module.
      $type = 'subscription';
      drupal_alter('payment_details', $payment_details, $order, $type);     

      $params = array(
        'endpoint' => 'subscriptions',
        'action' => 'create',
        'mandate' => $mandate,
        'amount' => $payment_details['amount'],
        'currency' => $payment_details['currency'],
        'name' => $payment_details['name'],
        'interval' => $payment_details['interval'],
        'interval_unit' => $payment_details['interval_unit'],
        'metadata' => $payment_details['metadata'],
      );
      if (!$product_uc->raise_payment) {
        $params['start_date'] = format_date($start_date, 'gocardless');
      }

      $result = uc_gc_client_api($params);

      if ($result->response->status_code == 201) {

        $sub = $result->response->body->subscriptions;
        $comment_arr = array(
          '@product' => $product->title,
          '@interval' => $sub->interval,
          '@interval_unit' => $sub->interval_unit,
          '@amount' => uc_currency_format($sub->amount / 100, $currency_sign),
          '@start_date' => format_date(strtotime($sub->start_date), 'uc_store'),
        );

        $comment = t('Your @interval @interval_unit subscription plan of @amount for @product has been created with GoCardless, and the first payment will be made from your bank on @start_date.', $comment_arr);
        uc_order_comment_save($order_id, $uid, $comment, 'order', 'Pending', TRUE);
        drupal_set_message($comment);
      }
      else {
        drupal_set_message(t('Something went wrong creating your subscription with GoCardless. Please contact the site administrator for assistance.'), 'warning');
      }
    }

    // If it is not a subscription.
    elseif ($gc_type != 'S') {

      // If raise_payment is set for the product then raise the first payment immediately.
      if ($product_uc->raise_payment) {

        $calculate = uc_gc_client_price_calculate($order, $product->order_product_id, $next_paymentdate = NULL, $status = NULL);
        $currency_code = isset($calculate['currency']) ? $calculate['currency'] : variable_get('uc_currency_code');
        $currency_sign = isset($calculate['sign']) ? $calculate['sign'] : variable_get('uc_currency_sign');
        $order->data['initial_payment'] = uc_currency_format($calculate['amount'], $currency_sign);
        uc_order_save($order);  

        $payment_details = array(
          'amount' => $calculate['amount'],
          'currency' => $currency_code,
          'description' => 'Initial payment for ' . $product->title,
          'metadata' => array(
            'ucpid' => $ucpid,
          ),
        );
        // Provide hook so that payment details can be altered by another module.
        $type = 'payment';
        drupal_alter('payment_details', $payment_details, $order, $type);     
        $params = array(
          'endpoint' => 'payments',
          'action' => 'create',
          'mandate' => $mandate,
          'amount' => $payment_details['amount'],
          'currency' => $payment_details['currency'],
          'description' => $payment_details['description'],
          'metadata' => $payment_details['metadata'],
        );
        $result = uc_gc_client_api($params);

        if ($result->response->status_code == 201) {
          // Update next_payment field in gc_client table.
          $payment = $result->response->body->payments;

          isset($interval) ? $next_payment = strtotime('+' . $interval['string']) : $next_payment = NULL;

          $update = db_update('uc_gc_client')
            ->fields(array(
              'next_payment' => $next_payment,
              'updated' => REQUEST_TIME,
            ))
            ->condition('ucpid', $ucpid)
            ->execute();

          // Let everyone know what is going on.
          $comment_array = array(
            '@amount' => uc_currency_format($payment->amount / 100, $currency_sign),
            '@charge_date' => date('D d M Y', strtotime($result->response->body->payments->charge_date)),
            '@product' => $product->title,
          );
          $comment = t('An initial payment of @amount for @product has been created with GoCardless, and will be made from your bank on @charge_date.', $comment_array);
          uc_order_comment_save($order_id, $uid, $comment, 'order', 'Pending', TRUE);
          drupal_set_message($comment);

          // Provide a hook so that other modules can respond to payment
          //creation.
          module_invoke_all('payment_created', $payment, $order_id);     
        }
        else {
          drupal_set_message(t('An initial payment could not be created because authentication failed. We will try and raise it again later.'), 'error');

          // Update next_payment to now so that it will get picked up on next
          // cron run.
          $update = db_update('uc_gc_client')
            ->fields(array(
              'next_payment' => REQUEST_TIME,
              'updated' => REQUEST_TIME,
            ))
            ->condition('ucpid', $ucpid)
            ->execute();
        }
      }

      //Else if a start date is set for the product then defer the first payment creation.
      elseif (!is_null($start_date)) {

        // Update next_payment field in uc_gcsubs table.
        $update = db_update('uc_gc_client')
          ->fields(array(
            'next_payment' => $start_date,
            'updated' => REQUEST_TIME,
          ))
          ->condition('ucpid', $ucpid)
          ->execute();

        $calculate = uc_gc_client_price_calculate($order, $product->order_product_id, $next_paymentdate = NULL, $status = NULL);
        //$calculate['amount'] = $product->price;
        $currency_code = isset($calculate['currency']) ? $calculate['currency'] : variable_get('uc_currency_code');
        $currency_sign = isset($calculate['sign']) ? $calculate['sign'] : variable_get('uc_currency_sign');
        $order->data['initial_payment'] = uc_currency_format($calculate['amount'], $currency_sign);
        uc_order_save($order);  

        // Let everyone know what is going on.
        $comment = t('A payment for @amount will be created with GoCardless on @start_date.', array('@amount' => uc_currency_format($calculate['amount'], $currency_sign), '@start_date' => format_date($start_date, 'uc_store')));
        uc_order_comment_save($order_id, $uid, $comment, 'order', 'Pending', TRUE);
        drupal_set_message($comment);
      }
    }
  }

  // This lets us know it's a legitimate access of the complete page.
  $_SESSION['uc_checkout'][$_SESSION['cart_order']]['do_complete'] = TRUE;
  drupal_goto('cart/checkout/complete');
}

/**
 * Menu callback; respond to a webhook from GoCardless.com.
 */
function uc_gc_client_webhook() {

  variable_get('uc_gc_client_sandbox') ? $secret = variable_get('uc_gc_client_webhook_sandbox') : $secret = variable_get('uc_gc_client_webhook_live');
  $webhook = file_get_contents('php://input');

  if (!function_exists('getallheaders')) {
    function getallheaders() {
      $headers = [];
      foreach ($_SERVER as $name => $value) {
        if (substr($name, 0, 5) == 'HTTP_') {
          $headers[str_replace(' ', '-', ucwords(strtolower(str_replace('_', ' ', substr($name, 5)))))] = $value;
        }
      }
      return $headers;
    }
  }

  $headers = getallheaders();
  $provided_signature = $headers["Webhook-Signature"];
  $calculated_signature = hash_hmac("sha256", $webhook, $secret);
  if ($provided_signature == $calculated_signature) {

    $data = json_decode($webhook, TRUE);

    // Optionally write webhook to log.
    if (variable_get('uc_gc_client_debug_webhook', FALSE)) {
      watchdog("uc_gc_client", '<pre>GoCardless webhook: <br />' . print_r($data, TRUE) . '</pre>');
    }

    //Send a success header before processing webhooks, in order to
    //prevent timeouts.
    header('HTTP/1.1 200 OK');

    // Process the events.
    foreach ($data['events'] as $event) {

      switch ($event['resource_type']) {

        case 'mandates':
          $order_id = uc_gc_client_id($event['links']['mandate']);
          $order = uc_order_load($order_id);
          $resource = array();
          uc_gc_client_webhook_mandates($order, $event['action'], $event);
          break;

        case 'subscriptions':
          $order_id = uc_gc_client_id($event['links']['mandate']);
          $order = uc_order_load($order_id);
          $resource = array();
          uc_gc_client_webhook_subscriptions($order, $event['action'], $event);
          break;

        case 'payments':
          $payment_id = $event['links']['payment'];
          $params = array(
            'endpoint' => 'payments',
            'action' => 'get',
            'id' => $payment_id,
          );
          $result = uc_gc_client_api($params);
          $resource = $result->response->body->payments;
          $order_id = uc_gc_client_id($resource->links->mandate);
          $order = uc_order_load($order_id);
          uc_gc_client_webhook_payments($order, $event['action'], $event, $resource);
          break;

        default:
          $order_id = FALSE;
          break;

      }

      // Invoke Rules event.
      if (module_exists('rules') && $order_id) {
        rules_invoke_event('uc_gc_client_webhook', json_encode($event), json_encode($resource), $order_id);
      }
    }
  }
  else {
    header("HTTP/1.1 498 Invalid Token");
  }
}

/**
 * Processes 'mandates' webhooks.
 */
function uc_gc_client_webhook_mandates($order, $action, $event) {

  switch ($action) {

    case 'submitted':
      if ($order->order_status <> 'processing') {
        $comment = t('Your direct debit mandate @mandate has been submitted to your bank by GoCardless and will be processed soon.', array('@mandate' => $event['links']['mandate']));
        uc_order_comment_save($order->order_id, $order->uid, $comment, 'order', 'processing', FALSE);
        uc_order_update_status($order->order_id, 'processing');
        // Update the status on the database for all products in this order.
        $update = db_update('uc_gc_client')
          ->fields(array(
            'status' => 'pending',
            'updated' => REQUEST_TIME,
          ))
          ->condition('ucid', $order->order_id)
          ->execute();
      }
      break;

    case 'failed':

      if ($order->order_status <> 'mandate_failed') {

        $comment = t('Your direct debit mandate @mandate creation has failed.', array('@mandate' => $event['links']['mandate']));
        uc_order_comment_save($order->order_id, $order->uid, $comment, 'order', 'processing', TRUE);

        uc_order_update_status($order->order_id, 'mandate_failed');
      }
      break;

    case 'active':

      if ($order->order_status <> 'mandate_active') {
        $comment = t('Your direct debit mandate @mandate has been set up successfully with your bank.', array('@mandate' => $event['links']['mandate']));
        uc_order_comment_save($order->order_id, $order->uid, $comment, 'order', 'Completed', TRUE);
        uc_order_update_status($order->order_id, 'mandate_active');
        $update = db_update('uc_gc_client')
          ->fields(array(
            'status' => 'completed',
            'updated' => REQUEST_TIME,
          ))
          ->condition('ucid', $order->order_id)
          ->execute();
      }
      break;

    case 'cancelled':

      if ($order->order_status <> 'canceled') {

        $comment = t('Your direct debit mandate @mandate has been cancelled with your bank by GoCardless.', array('@mandate' => $event['links']['mandate']));
        uc_order_comment_save($order->order_id, $order->uid, $comment, 'order', 'canceled', TRUE);

        uc_order_update_status($order->order_id, 'canceled');
        //$order->order_status = 'canceled';
        //uc_order_save($order);

        $update = db_update('uc_gc_client')
          ->fields(array(
            'status' => 'canceled',
            'updated' => REQUEST_TIME,
          ))
          ->condition('ucid', $order->order_id)
          ->execute();
      }
      break;

    case 'reinstated':
      if ($order->order_status <> 'processing') {
        $comment = t('Your direct debit mandate @mandate has been reinstated at GoCardless.', array('@mandate' => $event['links']['mandate']));
        uc_order_comment_save($order->order_id, $order->uid, $comment, 'order', 'processing', FALSE);
        uc_order_update_status($order->order_id, 'processing');
        $update = db_update('uc_gc_client')
          ->fields(array(
            'status' => 'pending',
            'updated' => REQUEST_TIME,
          ))
          ->condition('ucid', $order->order_id)
          ->execute();
      }
      break;
  }
}

/**
 * Processes 'payments' webhooks.
 */
function uc_gc_client_webhook_payments($order, $action, $event, $resource) {

  $mandate = $resource->links->mandate;
  $order_id = $order->order_id;
  $amount = $resource->amount/100;
  $status = db_select('uc_orders', 'o')
    ->fields('o', array('order_status'))
    ->condition('order_id', $order_id)
    ->execute()->fetchField();
  
  !empty($order->billing_country) ? $country_code = $order->billing_country : $country_code = $order->delivery_country;
  $currency_sign = uc_gc_client_currency($country_code)['sign']; 

  switch ($action) {

    case 'confirmed':
      uc_payment_enter($order_id, 'gc_client', $amount, 0, NULL, t('Direct debit has been taken by GoCardless'));
      $comment = t('Your payment of @amount has been confirmed by GoCardless and will be paid from your bank account.', array('@amount' => uc_currency_format($amount, $currency_sign)));
      uc_order_comment_save($order_id, 0, $comment, 'order', $status, TRUE);
      //Update status to payment_received if it is the first one
      if ($order->order_status == 'mandate_active') 
        uc_order_update_status($order->order_id, 'payment_received');
      break;

    case 'cancelled':
      $comment = t("Your direct debit payment '@id' for @amount has been cancelled at GoCardless.", array('@id' => $event['id'], '@amount' => uc_currency_format($amount, $currency_sign)));
      uc_order_comment_save($order_id, 0, $comment, 'order', $status, TRUE);
      break;
  }
}

/**
 * Processes 'subscription' webhooks.
 */
function uc_gc_client_webhook_subscriptions($order, $action, $event) {
  /*
  switch ($action) {

  case 'cancelled' :
  foreach ($items as $item) {
  isset($item['source_id']) ? $gc_order_id = $item['source_id'] : $gc_order_id = $item['id'];
  $order_id = uc_gc_client_id($gc_order_id);
  $order = uc_order_load($order_id);
  uc_order_update_status($order_id, 'canceled');
  uc_order_comment_save($order_id, $order->uid, t('This direct debit Subscription has been cancelled with GoCardless.com.'), 'order', 'canceled', TRUE);
  // update the status on the database
  $update = db_update('uc_gcsubs')
  ->fields(array(
  'status' => 'canceled',
  'updated' => time(),
  ))
  ->condition('ucid', $order_id, '=')
  ->execute();
  }

  // Invoke Rules event
  //if (module_exists('rules')) {
  //  $items_string = json_encode($items);
  //  rules_invoke_event('uc_gcsubs_subs_cancellation', $items_string);
  //}
  break;
  }
   */
}
